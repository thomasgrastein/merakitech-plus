/* merakiplus_upd.c — signed release manifest verifier for Meraki+ (docs/signing.md, docs/ota-plan.md phase 2).
 *
 * The on-device half of "one verifier": checks manifest.sig (Ed25519, signify format) against the public keys built
 * into this binary, validates manifest.json (format 1, the same rules as tools/merakiplus.py check_manifest), refuses
 * older releases (seq < --max-seq) and hashes files against the manifest. No network, no daemon, no writes.
 *
 *   merakiplus_upd --version | --selftest | keys
 *   merakiplus_upd sha256 FILE...                          (busybox on the display may lack sha256sum)
 *   merakiplus_upd verify MANIFEST SIG [--max-seq N] [--version V] [--assets DIR] [--release-dir DIR]
 *
 * verify prints one line: "ok version=<v> seq=<n> key=<id> files=<checked>" (exit 0) or
 * "fail <signature|format|older|asset|io> <reason>" (exit 3|4|5|6|7). Usage errors exit 2.
 *
 * Crypto: src/merakiplus_crypto.zig (Zig standard library). Phase 3 (on-device fetch) builds on this binary.
 * Build: ./build.sh (host build/merakiplus_upd_host, device build/merakiplus_upd). Tests: tests/test_upd.py.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char MARK[] = "@(#)merakiplus-upd 1";      /* what(1) marker, also --version */
#define REPO "thomasgrastein/merakitech-plus"
#define MANIFEST_MAX 65536
#define SIG_MAX 4096
#define SEQ_MAX 2147483647LL

int mp_ed25519_verify(const unsigned char *sig, const unsigned char *msg, size_t len, const unsigned char *pk);
void mp_sha256_init(void);                       /* one context, in merakiplus_crypto.zig */
void mp_sha256_update(const void *p, size_t n);
void mp_sha256_final(unsigned char *out);

/* ------------------------------------------------------------ trusted keys */
typedef struct { const char *id, *pk; } Key;     /* hex: 8-byte signify key number, 32-byte Ed25519 public key */
#include "merakiplus_upd_keys.h"
static const Key keys[] = {
    MERAKIPLUS_TRUSTED_KEYS
#ifdef MERAKIPLUS_UPD_TEST_KEYID                /* test builds only (tests/test_upd.py); never in build.sh output */
    {MERAKIPLUS_UPD_TEST_KEYID, MERAKIPLUS_UPD_TEST_PK},
#endif
    {0, 0}
};

/* ------------------------------------------------------------ errors */
enum { E_OK = 0, E_USAGE = 2, E_SIG = 3, E_FMT = 4, E_OLD = 5, E_ASSET = 6, E_IO = 7 };
static const char *ekind(int e)
{
    return e == E_SIG ? "signature" : e == E_FMT ? "format" : e == E_OLD ? "older" : e == E_ASSET ? "asset" : "io";
}
static char emsg[256];
static int fail(int e, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vsnprintf(emsg, sizeof emsg, fmt, ap); va_end(ap);
    return e;
}

/* ------------------------------------------------------------ helpers */
static int unhex(const char *h, unsigned char *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v = 0;
        for (int k = 0; k < 2; k++) {
            char c = h[2 * i + k]; v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else return -1;
        }
        out[i] = (unsigned char)v;
    }
    return h[2 * n] ? -1 : 0;
}
static void tohex(const unsigned char *b, size_t n, char *out)
{
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[2 * i] = d[b[i] >> 4]; out[2 * i + 1] = d[b[i] & 15]; }
    out[2 * n] = 0;
}
/* strict base64 (RFC 4648, padded); returns length or -1 */
static int b64dec(const char *s, size_t n, unsigned char *out, size_t cap)
{
    if (n % 4) return -1;
    size_t o = 0;
    for (size_t i = 0; i < n; i += 4) {
        unsigned v = 0; int pad = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[i + k]; int x;
            if (c >= 'A' && c <= 'Z') x = c - 'A'; else if (c >= 'a' && c <= 'z') x = c - 'a' + 26;
            else if (c >= '0' && c <= '9') x = c - '0' + 52; else if (c == '+') x = 62; else if (c == '/') x = 63;
            else if (c == '=' && i + 4 == n && k >= 2) { x = 0; pad++; }
            else return -1;
            if (pad && c != '=') return -1;
            v = v << 6 | (unsigned)x;
        }
        unsigned char t[3] = {(unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v};
        for (int k = 0; k < 3 - pad; k++) { if (o >= cap) return -1; out[o++] = t[k]; }
    }
    return (int)o;
}
static long slurp(const char *path, char *buf, size_t cap)          /* whole file, NUL-terminated; -1 = error/too big */
{
    errno = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    size_t n = 0; ssize_t r;
    while ((r = read(fd, buf + n, cap - n)) > 0) { n += (size_t)r; if (n == cap) break; }
    int e = errno;
    close(fd);
    if (r < 0) { errno = e; return -1; }
    if (n == cap) { errno = EFBIG; return -1; }
    buf[n] = 0; return (long)n;
}
/* size + SHA-256 of a file (streamed: nothing large in RAM) */
static int hash_file(const char *path, long long *size, unsigned char out[32])
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    mp_sha256_init();
    static unsigned char b[16384]; ssize_t r; long long n = 0;
    while ((r = read(fd, b, sizeof b)) > 0) { mp_sha256_update(b, (size_t)r); n += r; }
    close(fd);
    if (r < 0) return -1;
    mp_sha256_final(out); *size = n; return 0;
}

/* ------------------------------------------------------------ strict JSON (the subset tools/merakiplus.py emits) */
/* objects, arrays, strings of printable ASCII without '"' or '\\' (no escapes at all), non-negative integers without
 * leading zeros, true/false/null. Duplicate keys, floats, escapes, trailing data and depth > 8 are rejected. */
enum { J_OBJ, J_ARR, J_STR, J_INT, J_TRUE, J_FALSE, J_NULL };
typedef struct { int t, child, next, klen, slen; const char *key, *s; long long i; } J;
#define JMAX 4096
static J jn[JMAX];
static int jcount;
static const char *jp, *jend;
static void ws(void) { while (jp < jend && (*jp == ' ' || *jp == '\n' || *jp == '\t' || *jp == '\r')) jp++; }
static int jstr(const char **s, int *len)
{
    if (jp >= jend || *jp != '"') return -1;
    const char *b = ++jp;
    while (jp < jend && *jp != '"') { if (*jp < 0x20 || *jp > 0x7e || *jp == '\\') return -1; jp++; }
    if (jp >= jend) return -1;
    *s = b; *len = (int)(jp - b); jp++; return 0;
}
static int jval(int depth)
{
    if (depth > 8 || jcount >= JMAX) return -1;
    ws();
    if (jp >= jend) return -1;
    int me = jcount++; J *n = &jn[me]; memset(n, 0, sizeof *n); n->child = n->next = -1;
    if (*jp == '{' || *jp == '[') {
        int obj = *jp++ == '{'; n->t = obj ? J_OBJ : J_ARR;
        ws();
        if (jp < jend && *jp == (obj ? '}' : ']')) { jp++; return me; }
        int last = -1;
        for (;;) {
            const char *k = 0; int kl = 0;
            if (obj) {
                ws(); if (jstr(&k, &kl)) return -1;
                for (int c = jn[me].child; c >= 0; c = jn[c].next)
                    if (jn[c].klen == kl && !memcmp(jn[c].key, k, (size_t)kl)) return -1;    /* duplicate key */
                ws(); if (jp >= jend || *jp++ != ':') return -1;
            }
            int c = jval(depth + 1); if (c < 0) return -1;
            jn[c].key = k; jn[c].klen = kl;
            if (last < 0) jn[me].child = c; else jn[last].next = c;
            last = c;
            ws();
            if (jp < jend && *jp == ',') { jp++; continue; }
            if (jp < jend && *jp == (obj ? '}' : ']')) { jp++; return me; }
            return -1;
        }
    }
    if (*jp == '"') { n->t = J_STR; return jstr(&n->s, &n->slen) ? -1 : me; }
    if (*jp >= '0' && *jp <= '9') {
        if (*jp == '0' && jp + 1 < jend && jp[1] >= '0' && jp[1] <= '9') return -1;
        n->t = J_INT; int d = 0;
        while (jp < jend && *jp >= '0' && *jp <= '9') { if (++d > 15) return -1; n->i = n->i * 10 + (*jp++ - '0'); }
        if (jp < jend && (*jp == '.' || *jp == 'e' || *jp == 'E')) return -1;
        return me;
    }
    static const struct { const char *w; int t; } lit[] = {{"true", J_TRUE}, {"false", J_FALSE}, {"null", J_NULL}};
    for (int k = 0; k < 3; k++) {
        size_t l = strlen(lit[k].w);
        if ((size_t)(jend - jp) >= l && !memcmp(jp, lit[k].w, l)) { jp += l; n->t = lit[k].t; return me; }
    }
    return -1;
}
static int jparse(const char *b, size_t n)
{
    jcount = 0; jp = b; jend = b + n;
    int r = jval(0); ws();
    return (r < 0 || jp != jend) ? -1 : r;
}
/* canonical re-serialization (= Python json.dumps(sort_keys=True, indent=1, separators=(",", ": ")) + "\n") */
static char cbuf[MANIFEST_MAX + 16];
static size_t co;
static int put(const char *s, size_t n) { if (co + n > sizeof cbuf) return -1; memcpy(cbuf + co, s, n); co += n; return 0; }
static int indent(int lvl) { if (put("\n", 1)) return -1; for (int i = 0; i < lvl; i++) if (put(" ", 1)) return -1; return 0; }
static int keycmp(const J *a, const J *b)
{
    int m = a->klen < b->klen ? a->klen : b->klen, c = memcmp(a->key, b->key, (size_t)m);
    return c ? c : a->klen - b->klen;
}
static int canon(int x, int lvl)
{
    J *n = &jn[x]; char t[24];
    switch (n->t) {
    case J_STR: return put("\"", 1) || put(n->s, (size_t)n->slen) || put("\"", 1);
    case J_INT: snprintf(t, sizeof t, "%lld", n->i); return put(t, strlen(t));
    case J_TRUE: return put("true", 4);
    case J_FALSE: return put("false", 5);
    case J_NULL: return put("null", 4);
    }
    int obj = n->t == J_OBJ;
    if (n->child < 0) return put(obj ? "{}" : "[]", 2);
    if (put(obj ? "{" : "[", 1)) return -1;
    for (int c = n->child, first = 1, prev = -1; c >= 0; prev = c, c = jn[c].next, first = 0) {
        if (obj && prev >= 0 && keycmp(&jn[prev], &jn[c]) >= 0) return -1;            /* keys must be sorted */
        if ((!first && put(",", 1)) || indent(lvl + 1)) return -1;
        if (obj && (put("\"", 1) || put(jn[c].key, (size_t)jn[c].klen) || put("\": ", 3))) return -1;
        if (canon(c, lvl + 1)) return -1;
    }
    return indent(lvl) || put(obj ? "}" : "]", 1);
}
static int jget(int obj, const char *k)
{
    size_t l = strlen(k);
    for (int c = jn[obj].child; c >= 0; c = jn[c].next) if ((size_t)jn[c].klen == l && !memcmp(jn[c].key, k, l)) return c;
    return -1;
}
static int jis(int x, const char *s) { return x >= 0 && jn[x].t == J_STR && (size_t)jn[x].slen == strlen(s) && !memcmp(jn[x].s, s, strlen(s)); }
static int jcpy(int x, char *o, size_t cap)
{
    if (x < 0 || jn[x].t != J_STR || (size_t)jn[x].slen >= cap) return -1;
    memcpy(o, jn[x].s, (size_t)jn[x].slen); o[jn[x].slen] = 0; return 0;
}

/* ------------------------------------------------------------ manifest rules (format 1) */
static int safe_name(const char *s, size_t n)                     /* [A-Za-z0-9_][A-Za-z0-9._+-]{0,63} */
{
    if (n < 1 || n > 64) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i]; int al = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!al && (i == 0 || (c != '.' && c != '+' && c != '-'))) return 0;
    }
    return 1;
}
static int safe_path(const char *s, size_t n)                     /* safe_name components separated by '/' */
{
    if (n > 128) return 0;
    size_t b = 0;
    for (size_t i = 0; i <= n; i++) if (i == n || s[i] == '/') { if (!safe_name(s + b, i - b)) return 0; b = i + 1; }
    return 1;
}
static int is_hex64(int x)
{
    if (x < 0 || jn[x].t != J_STR || jn[x].slen != 64) return 0;
    for (int i = 0; i < 64; i++) { char c = jn[x].s[i]; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0; }
    return 1;
}
static int is_version(const char *v)                              /* \d{1,4}.\d{1,4}(.\d{1,4})? */
{
    int parts = 0;
    for (const char *p = v;;) {
        int d = 0; while (*p >= '0' && *p <= '9') { p++; d++; }
        if (d < 1 || d > 4) return 0;
        parts++;
        if (!*p) return parts >= 2 && parts <= 3;
        if (*p++ != '.') return 0;
    }
}
static int int_in(int x, long long lo, long long hi) { return x >= 0 && jn[x].t == J_INT && jn[x].i >= lo && jn[x].i <= hi; }

typedef struct { char version[16]; long long seq; int root, assets, release; } Manifest;

static int check_manifest(const char *b, size_t n, Manifest *m)
{
    int r = jparse(b, n);
    if (r < 0 || jn[r].t != J_OBJ) return fail(E_FMT, "manifest: not strict JSON");
    co = 0;
    if (canon(r, 0) || put("\n", 1) || co != n || memcmp(cbuf, b, n)) return fail(E_FMT, "manifest: not in canonical form");
    if (!int_in(jget(r, "format"), 1, 1)) return fail(E_FMT, "manifest: unsupported format");
    if (!jis(jget(r, "product"), "merakiplus") || !jis(jget(r, "channel"), "stable")) return fail(E_FMT, "manifest: wrong product/channel");
    char tag[24], want[160], t[160];
    if (jcpy(jget(r, "version"), m->version, sizeof m->version) || !is_version(m->version)) return fail(E_FMT, "manifest: bad version");
    snprintf(tag, sizeof tag, "v%s", m->version);
    if (!jis(jget(r, "tag"), tag)) return fail(E_FMT, "manifest: tag does not match version");
    int s = jget(r, "seq");
    if (!int_in(s, 1, SEQ_MAX)) return fail(E_FMT, "manifest: seq missing or out of range");
    m->seq = jn[s].i;
    int c = jget(r, "commit");
    if (jcpy(c, t, sizeof t) || strlen(t) != 40 || strspn(t, "0123456789abcdef") != 40) return fail(E_FMT, "manifest: bad commit");
    int d = jget(r, "released");
    if (jcpy(d, t, sizeof t) || strlen(t) != 10 || strspn(t, "0123456789") != 4 || t[4] != '-' || t[7] != '-' ||
        strspn(t + 5, "0123456789") != 2 || strspn(t + 8, "0123456789") != 2) return fail(E_FMT, "manifest: bad released date");
    snprintf(want, sizeof want, "https://github.com/" REPO "/releases/download/%s/", tag);
    if (!jis(jget(r, "base_url"), want)) return fail(E_FMT, "manifest: base_url does not pin this repository's tag");
    int a = jget(r, "assets");
    if (a < 0 || jn[a].t != J_ARR || jn[a].child < 0) return fail(E_FMT, "manifest: no assets");
    for (int e = jn[a].child; e >= 0; e = jn[e].next) {
        int nm = jget(e, "name");
        if (jn[e].t != J_OBJ || nm < 0 || jn[nm].t != J_STR || !safe_name(jn[nm].s, (size_t)jn[nm].slen) ||
            (jn[nm].slen >= 8 && !memcmp(jn[nm].s, "manifest", 8)) || !int_in(jget(e, "size"), 0, SEQ_MAX) || !is_hex64(jget(e, "sha256")))
            return fail(E_FMT, "manifest: bad asset entry");
        for (int f = jn[a].child; f != e; f = jn[f].next)
            if (jn[jget(f, "name")].slen == jn[nm].slen && !memcmp(jn[jget(f, "name")].s, jn[nm].s, (size_t)jn[nm].slen))
                return fail(E_FMT, "manifest: duplicate asset");
    }
    static const char *sects[] = {"release", "platform"};
    for (int k = 0; k < 2; k++) {
        int x = jget(r, sects[k]);
        if (x < 0 || jn[x].t != J_ARR || jn[x].child < 0) return fail(E_FMT, "manifest: no %s section", sects[k]);
        for (int e = jn[x].child; e >= 0; e = jn[e].next) {
            int nm = jget(e, "name"), src = jget(e, "src");
            if (jn[e].t != J_OBJ || nm < 0 || jn[nm].t != J_STR || !safe_path(jn[nm].s, (size_t)jn[nm].slen) || src < 0 || jn[src].t != J_STR)
                return fail(E_FMT, "manifest: bad %s entry", sects[k]);
            for (int f = jn[x].child; f != e; f = jn[f].next)
                if (jn[jget(f, "name")].slen == jn[nm].slen && !memcmp(jn[jget(f, "name")].s, jn[nm].s, (size_t)jn[nm].slen))
                    return fail(E_FMT, "manifest: duplicate %s entry", sects[k]);
            int hit = 0;
            for (int f = jn[a].child; f >= 0; f = jn[f].next) {
                int fn = jget(f, "name");
                if (jn[fn].slen != jn[src].slen || memcmp(jn[fn].s, jn[src].s, (size_t)jn[src].slen)) continue;
                int sz = jget(e, "size"), h = jget(e, "sha256");
                hit = sz >= 0 && jn[sz].t == J_INT && jn[sz].i == jn[jget(f, "size")].i && h >= 0 && jn[h].t == J_STR &&
                      jn[h].slen == 64 && !memcmp(jn[h].s, jn[jget(f, "sha256")].s, 64);
            }
            if (!hit) return fail(E_FMT, "manifest: %s entry does not match its asset", sects[k]);
            if (k == 0 && !jis(jget(e, "mode"), "644") && !jis(jget(e, "mode"), "755")) return fail(E_FMT, "manifest: bad mode");
        }
    }
    m->root = r; m->assets = a; m->release = jget(r, "release");
    return E_OK;
}

/* ------------------------------------------------------------ signature */
static int check_sig(const char *man, size_t mlen, const char *sigtxt, char keyid[17])
{
    const char *nl = strchr(sigtxt, '\n');
    if (strncmp(sigtxt, "untrusted comment: ", 19) || !nl) return fail(E_SIG, "not a signify signature file");
    const char *l2 = nl + 1; size_t n = strcspn(l2, "\n");
    if (l2[n] == '\n' && l2[n + 1]) return fail(E_SIG, "trailing data in the signature file");
    unsigned char raw[80];
    if (b64dec(l2, n, raw, sizeof raw) != 74 || raw[0] != 'E' || raw[1] != 'd') return fail(E_SIG, "not an Ed25519 signify signature");
    tohex(raw + 2, 8, keyid);
    for (const Key *k = keys; k->id; k++) {
        if (strcmp(k->id, keyid)) continue;
        unsigned char pk[32];
        if (unhex(k->pk, pk, 32)) return fail(E_SIG, "bad built-in key %s", k->id);
        if (mp_ed25519_verify(raw + 10, (const unsigned char *)man, mlen, pk)) return fail(E_SIG, "BAD SIGNATURE (key id %s)", keyid);
        return E_OK;
    }
    return fail(E_SIG, "signed by an unknown key (id %s)", keyid);
}

static int check_files(int list, const char *dir, int *count)      /* entries of `list`, by "name", inside dir */
{
    for (int e = jn[list].child; e >= 0; e = jn[e].next) {
        char name[160], path[512], want[65], got[65]; unsigned char h[32]; long long size;
        if (jcpy(jget(e, "name"), name, sizeof name) || jcpy(jget(e, "sha256"), want, sizeof want)) return fail(E_FMT, "manifest entry");
        snprintf(path, sizeof path, "%s/%s", dir, name);
        if (hash_file(path, &size, h)) return fail(E_ASSET, "%s: missing (%s)", name, strerror(errno));
        tohex(h, 32, got);
        if (size != jn[jget(e, "size")].i || strcmp(got, want)) return fail(E_ASSET, "%s does not match the signed manifest", name);
        (*count)++;
    }
    return E_OK;
}

static int verify(int argc, char **argv)
{
    if (argc < 4) return E_USAGE;
    long long max_seq = 0; const char *want_ver = 0, *assets = 0, *reldir = 0;
    for (int i = 4; i < argc; i++) {
        if (i + 1 >= argc) return E_USAGE;
        if (!strcmp(argv[i], "--max-seq")) { char *e; max_seq = strtoll(argv[++i], &e, 10); if (*e || max_seq < 0) return E_USAGE; }
        else if (!strcmp(argv[i], "--version")) want_ver = argv[++i];
        else if (!strcmp(argv[i], "--assets")) assets = argv[++i];
        else if (!strcmp(argv[i], "--release-dir")) reldir = argv[++i];
        else return E_USAGE;
    }
    static char man[MANIFEST_MAX + 1], sig[SIG_MAX + 1];
    long mlen = slurp(argv[2], man, sizeof man);
    if (mlen < 0) return errno == EFBIG ? fail(E_FMT, "manifest larger than %d bytes", MANIFEST_MAX)
                                        : fail(E_IO, "cannot read the manifest (%s)", strerror(errno));
    long slen = slurp(argv[3], sig, sizeof sig);
    if (slen < 0) return fail(E_SIG, "cannot read the signature file (missing signature)");
    char keyid[17]; int e;
    if ((e = check_sig(man, (size_t)mlen, sig, keyid))) return e;                 /* signature before parsing anything */
    Manifest m;
    if ((e = check_manifest(man, (size_t)mlen, &m))) return e;
    if (m.seq < max_seq) return fail(E_OLD, "OLDER VERSION REFUSED: seq %lld < %lld", m.seq, max_seq);
    if (want_ver && strcmp(want_ver, m.version)) return fail(E_FMT, "manifest is for v%s, expected v%s", m.version, want_ver);
    int files = 0;
    if (assets && (e = check_files(m.assets, assets, &files))) return e;
    if (reldir && (e = check_files(m.release, reldir, &files))) return e;
    printf("ok version=%s seq=%lld key=%s files=%d\n", m.version, m.seq, keyid, files);
    return E_OK;
}

/* ------------------------------------------------------------ selftest: RFC 8032 + FIPS 180-2 vectors, parser cases */
static int selftest(void)
{
    static const struct { const char *pk, *msg, *sig; } v[] = {
        {"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
        {"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
        {"fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
         "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
    };
    int bad = 0, n = 0;
#define T(name, cond) do { n++; if (!(cond)) { bad++; printf("FAIL %s\n", name); } } while (0)
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        unsigned char pk[32], sg[64], msg[8] = {0}; size_t ml = strlen(v[i].msg) / 2;
        unhex(v[i].pk, pk, 32); unhex(v[i].sig, sg, 64); if (ml) unhex(v[i].msg, msg, ml);
        T("rfc8032 valid", mp_ed25519_verify(sg, msg, ml, pk) == 0);
        msg[0] ^= 1; T("rfc8032 tampered message", mp_ed25519_verify(sg, msg, ml ? ml : 1, pk) != 0); msg[0] ^= 1;
        sg[63] ^= 0x40; T("rfc8032 tampered signature", mp_ed25519_verify(sg, msg, ml, pk) != 0); sg[63] ^= 0x40;
        pk[5] ^= 1; T("rfc8032 other key", mp_ed25519_verify(sg, msg, ml, pk) != 0);
    }
    unsigned char h[32]; char hx[65];
    mp_sha256_init(); mp_sha256_update("ab", 2); mp_sha256_update("c", 1); mp_sha256_final(h); tohex(h, 32, hx);
    T("sha256 abc", !strcmp(hx, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    mp_sha256_init(); mp_sha256_final(h); tohex(h, 32, hx);
    T("sha256 empty", !strcmp(hx, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    static const struct { const char *j; int ok; } js[] = {
        {"{\"a\": 1}", 1}, {"{\"a\":1,\"a\":2}", 0}, {"{\"a\": 1.5}", 0}, {"{\"a\": 01}", 0}, {"{\"a\": \"x\\n\"}", 0},
        {"{\"a\": [true, false, null]}", 1}, {"{\"a\": 1} x", 0}, {"[[[[[[[[[[1]]]]]]]]]]", 0}, {"{\"a\": -1}", 0}, {"", 0},
    };
    for (unsigned i = 0; i < sizeof js / sizeof *js; i++) T(js[i].j, (jparse(js[i].j, strlen(js[i].j)) >= 0) == js[i].ok);
    unsigned char b[80];
    T("base64 strict", b64dec("QUJD", 4, b, sizeof b) == 3 && b64dec("QUI=", 4, b, sizeof b) == 2 && b64dec("Q=I=", 4, b, sizeof b) < 0 &&
                       b64dec("QUJ", 3, b, sizeof b) < 0 && b64dec("QU J", 4, b, sizeof b) < 0);
    T("safe names", safe_path("audio/start.wav", 15) && !safe_path("../x", 4) && !safe_path("a//b", 4) && !safe_path(".x", 2) && safe_name("grab_shim.so", 12));
    T("version", is_version("6.23") && is_version("6.18.1") && !is_version("6") && !is_version("6.x") && !is_version("6.1.2.3"));
    int k = 0; for (const Key *p = keys; p->id; p++) { unsigned char t[32]; T("built-in key well-formed", strlen(p->id) == 16 && !unhex(p->pk, t, 32)); k++; }
    T("at least one built-in key", k > 0);
    printf("merakiplus_upd selftest: %d/%d passed\n", n - bad, n);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--version")) { printf("%s\n", &MARK[4]); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--selftest")) return selftest();
    if (argc == 2 && !strcmp(argv[1], "keys")) { for (const Key *k = keys; k->id; k++) printf("%s %s\n", k->id, k->pk); return 0; }
    if (argc >= 3 && !strcmp(argv[1], "sha256")) {
        int rc = 0;
        for (int i = 2; i < argc; i++) {
            unsigned char h[32]; char hx[65]; long long sz;
            if (hash_file(argv[i], &sz, h)) { fprintf(stderr, "merakiplus_upd: %s: %s\n", argv[i], strerror(errno)); rc = E_IO; continue; }
            tohex(h, 32, hx); printf("%s  %s\n", hx, argv[i]);
        }
        return rc;
    }
    if (argc >= 2 && !strcmp(argv[1], "verify")) {
        int e = verify(argc, argv);
        if (e == E_USAGE) goto usage;
        if (e) printf("fail %s %s\n", ekind(e), emsg);
        return e;
    }
usage:
    fprintf(stderr, "usage: merakiplus_upd --version | --selftest | keys | sha256 FILE...\n"
                    "       merakiplus_upd verify MANIFEST SIG [--max-seq N] [--version V] [--assets DIR] [--release-dir DIR]\n");
    return E_USAGE;
}
