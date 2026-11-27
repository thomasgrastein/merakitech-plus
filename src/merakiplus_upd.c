/* merakiplus_upd.c — the Meraki+ updater: signed release manifests (docs/signing.md, docs/ota-plan.md phase 2) and the
 * on-device HTTPS check + fetch (phase 3).
 *
 * The on-device half of "one verifier": checks manifest.sig (Ed25519, signify format) against the public keys built
 * into this binary, validates manifest.json (format 1, the same rules as tools/merakiplus.py check_manifest), refuses
 * older releases and hashes files against the manifest. Phase 3 adds the network: `check` and `install` fetch the
 * manifest from the fixed discovery URL over HTTPS (src/merakiplus_upd_net.c: static BearSSL, pinned roots), verify the
 * signature and the anti-downgrade rule BEFORE any asset is downloaded, and `install` streams the release files into
 * rel/.stage-<name>/ (size + SHA-256 per file, fsync, re-hash from flash), writes the MANIFEST, renames the directory
 * into rel/ in one step and writes `pending=<name>` into ota/state (tmp + fsync + rename + fsync dir, under ota/lock).
 * The launcher (src/merakiplus_boot.c) starts the trial at its next start and rolls it back if it never gets healthy.
 * Anti-downgrade counts a network-installed release only once it is healthy: staging records `trial_seq=<name> <seq>`,
 * and that seq raises max_seq when (and only when) the launcher has committed <name> as `current`. A trial that was
 * rolled back therefore stays available and can be tried again; a genuinely older signed release is still refused.
 * No daemon, no listening socket: it runs on demand (the UI's SYSTEM › UPDATES, or a shell) and exits.
 *
 *   merakiplus_upd --version | --selftest | keys | notices   (notices: THIRD-PARTY-NOTICES, built in)
 *   merakiplus_upd sha256 FILE...                          (busybox on the display may lack sha256sum)
 *   merakiplus_upd verify MANIFEST SIG [--max-seq N] [--version V] [--assets DIR] [--release-dir DIR]
 *   merakiplus_upd check   [--status FILE]                 (network: is a newer signed release available? downloads
 *                                                           only manifest.json + .sig (+ notes.txt); writes nothing on flash)
 *   merakiplus_upd install --version V [--status FILE] [--running NAME]
 *                                                          (network: fetch + verify + stage that release as the trial)
 *
 * verify prints one line: "ok version=<v> seq=<n> key=<id> files=<checked>" (exit 0) or
 * "fail <signature|format|older|asset|io> <reason>" (exit 3|4|5|6|7). Usage errors exit 2.
 * check / install print "ok <result> ..." or "fail <kind> <reason>" and keep the status file (key=value, replaced
 * atomically, in /tmp) up to date for the UI: op, state (running|ok|fail), phase, result, error, msg, version, seq,
 * current, name, blocked, pct, notes1..notes4. Exit codes as verify, plus 8 = network, 9 = busy / not possible now.
 *
 * Crypto: src/merakiplus_crypto.zig (Zig standard library) for Ed25519 + SHA-256; TLS: third_party/bearssl.
 * Build: ./build.sh (host build/merakiplus_upd_host, test build build/merakiplus_upd_test, device build/merakiplus_upd).
 * Tests: tests/test_upd.py (verifier), tests/test_ota_fetch.py (network, staging, state; local TLS server).
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "merakiplus_upd_net.h"

static const char MARK[] = "@(#)merakiplus-upd 2";      /* what(1) marker, also --version (2 = phase 3: check/install) */
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
enum { E_OK = 0, E_USAGE = 2, E_SIG = 3, E_FMT = 4, E_OLD = 5, E_ASSET = 6, E_IO = 7, E_NET = 8, E_BUSY = 9 };
static const char *ekind(int e)
{
    return e == E_SIG ? "signature" : e == E_FMT ? "format" : e == E_OLD ? "older" : e == E_ASSET ? "asset" : e == E_NET ? "net" :
           e == E_BUSY ? "busy" : "io";
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
#ifdef MERAKIPLUS_UPD_TEST                      /* test builds only: MERAKIPLUS_UPD_TEST_KEY=<id hex>:<public key hex> */
    const char *tk = getenv("MERAKIPLUS_UPD_TEST_KEY");
    if (tk && strlen(tk) == 16 + 1 + 64 && tk[16] == ':' && !strncmp(tk, keyid, 16)) {
        unsigned char pk[32];
        if (unhex(tk + 17, pk, 32)) return fail(E_SIG, "bad test key");
        if (mp_ed25519_verify(raw + 10, (const unsigned char *)man, mlen, pk)) return fail(E_SIG, "BAD SIGNATURE (key id %s)", keyid);
        return E_OK;
    }
#endif
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

/* ============================================================ phase 3: on-device check + install (docs/ota-plan.md §5, §6.2)
 * Paths are the launcher's (src/merakiplus_boot.c); test builds prefix them with MERAKIPLUS_UPD_ROOT. */
#define OURDIR     "/new_customer/merakiplus"
#define RELDIR     OURDIR "/rel"
#define STATE_F    OURDIR "/ota/state"
#define HEALTHY_F  OURDIR "/ota/healthy"
#define LOCK_F     OURDIR "/ota/lock"
#define LAUNCHER_F "/new_customer/demo_v9"
#define STATUS_F   "/tmp/merakiplus_upd.status"
#define RUNLOCK_F  "/tmp/merakiplus_upd.lock"
#define DISCOVERY  "https://github.com/" REPO "/releases/latest/download/"
#define NOTES_MAX  8192
#define UA         "merakiplus-upd/2"
#define CHECK_MS   90000LL                /* whole check (manifest + signature + notes, incl. redirects) */
#define INSTALL_MS 900000LL               /* whole install (~0.6 MB over a slow Wi-Fi) */

static char root[256];
static const char *P(const char *path)    /* root-prefixed path in one of 8 rotating buffers */
{
    static char b[8][600]; static int k;
    char *o = b[k++ & 7]; snprintf(o, 600, "%s%s", root, path); return o;
}
static long long mono_ms(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* ---- the status file: key=value, replaced atomically (tmpfs), read by the UI ---- */
static struct {
    const char *path, *op, *state, *phase, *result, *error, *blocked;
    char msg[200], version[16], current[64], name[64], notes[4][64];
    long long seq, done, total; int pct;
    long long t_last;
} S;
static void status_write(void)
{
    if (!S.path) return;
    char b[2048], tmp[620]; int n = 0;
#define KV(k, v) n += snprintf(b + n, sizeof b - (size_t)n, "%s=%s\n", k, (v) ? (v) : "")
    KV("op", S.op); KV("state", S.state); KV("phase", S.phase); KV("result", S.result); KV("error", S.error); KV("msg", S.msg);
    KV("version", S.version); n += snprintf(b + n, sizeof b - (size_t)n, "seq=%lld\n", S.seq); KV("current", S.current);
    KV("name", S.name); KV("blocked", S.blocked);
    n += snprintf(b + n, sizeof b - (size_t)n, "pct=%d\ndone=%lld\ntotal=%lld\n", S.pct, S.done, S.total);
    for (int i = 0; i < 4; i++) { char k[8]; snprintf(k, sizeof k, "notes%d", i + 1); KV(k, S.notes[i]); }
    n += snprintf(b + n, sizeof b - (size_t)n, "pid=%d\n", (int)getpid());
#undef KV
    snprintf(tmp, sizeof tmp, "%s.tmp", S.path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return;
    if (write(fd, b, (size_t)n) != n) { close(fd); unlink(tmp); return; }
    close(fd);
    if (rename(tmp, S.path)) unlink(tmp);
    S.t_last = mono_ms();
}
static void phase(const char *ph) { S.phase = ph; status_write(); }
static void clean_msg(char *m)            /* the status file is line-based: one printable ASCII line */
{
    for (char *p = m; *p; p++) if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7e) *p = ' ';
}
/* error kind for the UI (one word; the UI maps it to a toast) */
static const char *net_kind(int ne)
{
    return ne == NE_TLS ? "tls" : ne == NE_DNS ? "dns" : ne == NE_TIMEOUT ? "timeout" : ne == NE_NOTFOUND ? "notfound" :
           ne == NE_REDIRECT || ne == NE_URL ? "redirect" : ne == NE_LIMIT ? "limit" : ne == NE_TRUNC ? "truncated" :
           ne == NE_HTTP ? "http" : "net";
}
static int finish(int e, const char *kind)
{
    if (e == E_OK) { S.state = "ok"; S.error = ""; }
    else { S.state = "fail"; S.error = kind; snprintf(S.msg, sizeof S.msg, "%s", emsg); clean_msg(S.msg); }
    status_write();
    if (e == E_OK) printf("ok %s version=%s seq=%lld%s%s\n", S.result ? S.result : "", S.version, S.seq, S.name[0] ? " name=" : "", S.name);
    else printf("fail %s %s\n", kind, emsg);
    return e;
}

/* ---- ota/state: the launcher's format (format, current, previous, pending, tries, last + unknown lines kept) ---- */
typedef struct { int ok; char cur[64], prev[64], pend[64], last[160], extra[1024], trial[64]; int tries; long long max_seq, trial_seq; } St;
static void kvget(const char *txt, const char *key, char *o, size_t n)
{
    size_t kl = strlen(key); o[0] = 0;
    for (const char *p = txt; p && *p; ) {
        if (!strncmp(p, key, kl) && p[kl] == '=') { p += kl + 1; size_t l = strcspn(p, "\n"); if (l >= n) l = n - 1; memcpy(o, p, l); o[l] = 0; return; }
        p = strchr(p, '\n'); if (p) p++;
    }
}
static void state_read(St *s)
{
    static char b[4096]; memset(s, 0, sizeof *s);
    if (slurp(P(STATE_F), b, sizeof b) <= 0) return;
    char f[16], t[32]; kvget(b, "format", f, sizeof f); if (strcmp(f, "1")) return;
    kvget(b, "current", s->cur, sizeof s->cur); kvget(b, "previous", s->prev, sizeof s->prev);
    kvget(b, "pending", s->pend, sizeof s->pend); kvget(b, "last", s->last, sizeof s->last);
    kvget(b, "tries", t, sizeof t); s->tries = atoi(t);
    kvget(b, "max_seq", t, sizeof t); s->max_seq = atoll(t);
    /* trial_seq=<name> <seq>: the seq of the release this updater staged. It counts for anti-downgrade only once the
     * launcher committed it (current=<name>, i.e. it got healthy); after a rollback it is ignored, so a retry is possible */
    char ts[96]; kvget(b, "trial_seq", ts, sizeof ts);
    if (sscanf(ts, "%63s %lld", s->trial, &s->trial_seq) != 2 || s->trial_seq < 0) { s->trial[0] = 0; s->trial_seq = 0; }
    if (s->trial[0] && !strcmp(s->trial, s->cur) && s->trial_seq > s->max_seq) s->max_seq = s->trial_seq;
    static const char *known[] = {"format", "current", "previous", "pending", "tries", "last", "max_seq", "trial_seq"};
    size_t o = 0;
    for (char *l = b; l && *l; ) {
        char *e = strchr(l, '\n'); size_t ll = e ? (size_t)(e - l) : strlen(l); int k = 0;
        for (unsigned i = 0; i < sizeof known / sizeof *known; i++) { size_t kl = strlen(known[i]); if (ll > kl && !strncmp(l, known[i], kl) && l[kl] == '=') k = 1; }
        if (!k && ll && memchr(l, '=', ll) && o + ll + 2 < sizeof s->extra) { memcpy(s->extra + o, l, ll); o += ll; s->extra[o++] = '\n'; s->extra[o] = 0; }
        l = e ? e + 1 : NULL;
    }
    s->ok = 1;
}
static int fsync_dir(const char *path)
{
    char d[620]; snprintf(d, sizeof d, "%s", path);
    char *x = strrchr(d, '/'); if (!x) return -1; if (x == d) x[1] = 0; else *x = 0;
    int fd = open(d, O_RDONLY | O_CLOEXEC); if (fd < 0) return -1;
    int r = fsync(fd); close(fd); return r;
}
static int write_durable(const char *path, const char *data, size_t n)   /* tmp -> fsync -> rename -> fsync(dir) */
{
    char tmp[640]; snprintf(tmp, sizeof tmp, "%s.tmp.U", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644); if (fd < 0) return -1;
    size_t o = 0; while (o < n) { ssize_t w = write(fd, data + o, n - o); if (w <= 0) { close(fd); unlink(tmp); return -1; } o += (size_t)w; }
    if (fsync(fd)) { close(fd); unlink(tmp); return -1; }
    close(fd);
    if (rename(tmp, path)) { unlink(tmp); return -1; }
    return fsync_dir(path);
}
static int state_write(const St *s)
{
    char b[2048];
    int n = snprintf(b, sizeof b, "format=1\ncurrent=%s\nprevious=%s\npending=%s\ntries=%d\nlast=%s\nmax_seq=%lld\n",
                     s->cur, s->prev, s->pend, s->tries, s->last, s->max_seq);
    if (n > 0 && s->trial[0]) n += snprintf(b + n, n < (int)sizeof b ? sizeof b - (size_t)n : 0, "trial_seq=%s %lld\n", s->trial, s->trial_seq);
    if (n > 0 && n < (int)sizeof b) n += snprintf(b + n, sizeof b - (size_t)n, "%s", s->extra);
    if (n <= 0 || n >= (int)sizeof b) return -1;
    return write_durable(P(STATE_F), b, (size_t)n);
}
static int ota_lock(void)                 /* the launcher's ota/lock (it waits ≤ 2 s for it); we wait ≤ 10 s */
{
    int fd = open(P(LOCK_F), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    for (int i = 0; fd >= 0 && i < 100; i++) { if (!flock(fd, LOCK_EX | LOCK_NB)) return fd; usleep(100000); }
    if (fd >= 0) close(fd);
    return -1;
}
static void rel_version(const char *name, char *o, size_t n)   /* "6.25-bb620afa" -> "6.25" */
{
    size_t l = strcspn(name, "-"); if (l >= n) l = n - 1; memcpy(o, name, l); o[l] = 0;
}

/* ---- fetch the signed manifest from the discovery URL and verify it (signature first) ---- */
typedef struct { char *b; size_t n, cap; } Buf;
static int buf_sink(void *c, const unsigned char *p, size_t n)
{
    Buf *x = c; if (x->n + n > x->cap) return 1;
    memcpy(x->b + x->n, p, n); x->n += n; return 0;
}
static char man[MANIFEST_MAX + 1], sigt[SIG_MAX + 1];
static size_t man_len;
static int fetch_small(const char *url, Buf *dst, long long limit, long long deadline, int *ne)
{
    mp_http h; memset(&h, 0, sizeof h); h.ua = UA; h.deadline = deadline; h.limit = limit;
    dst->n = 0;
    int r = mp_https_get(&h, url, buf_sink, dst);
    *ne = r;
    if (r) { snprintf(emsg, sizeof emsg, "%s", h.err); return r == NE_NOTFOUND ? E_FMT : E_NET; }
    dst->b[dst->n] = 0;
    return E_OK;
}
static int fetch_manifest(Manifest *m, long long deadline, const char **kind)
{
    Buf mb = {man, 0, MANIFEST_MAX}, sb = {sigt, 0, SIG_MAX}; int ne, e;
    phase("manifest");
    if ((e = fetch_small(DISCOVERY "manifest.json", &mb, MANIFEST_MAX, deadline, &ne))) { *kind = ne == NE_NOTFOUND ? "none" : net_kind(ne); return e; }
    if ((e = fetch_small(DISCOVERY "manifest.sig", &sb, SIG_MAX, deadline, &ne))) {
        if (ne == NE_NOTFOUND) { *kind = "signature"; return fail(E_SIG, "the latest release has a manifest but no signature"); }
        *kind = net_kind(ne); return e;
    }
    man_len = mb.n;
    phase("verify");
    char keyid[17];
    if ((e = check_sig(man, man_len, sigt, keyid))) { *kind = "signature"; return e; }      /* signature before parsing anything */
    if ((e = check_manifest(man, man_len, m))) { *kind = "format"; return e; }
    snprintf(S.version, sizeof S.version, "%s", m->version); S.seq = m->seq;
    return E_OK;
}
static int rel_entry(const Manifest *m, const char *name)                 /* the "release" entry with this name */
{
    for (int e = jn[m->release].child; e >= 0; e = jn[e].next) if (jis(jget(e, "name"), name)) return e;
    return -1;
}
static int asset_entry(const Manifest *m, const char *name)
{
    for (int e = jn[m->assets].child; e >= 0; e = jn[e].next) if (jis(jget(e, "name"), name)) return e;
    return -1;
}
/* §5.6: the release needs the launcher it was built with; a different one is updated from a computer only */
static int launcher_ok(const Manifest *m)
{
    int pl = jget(m->root, "platform");
    for (int e = jn[pl].child; e >= 0; e = jn[e].next) {
        if (!jis(jget(e, "name"), "launcher")) continue;
        char want[65], got[65]; unsigned char h[32]; long long sz;
        if (jcpy(jget(e, "sha256"), want, sizeof want) || hash_file(P(LAUNCHER_F), &sz, h)) return 0;
        tohex(h, 32, got); return !strcmp(got, want);
    }
    return 0;
}
/* notes.txt (optional asset, written by tools/release_notes.py --device): the first four bullet lines, upper case */
static void fetch_notes(const Manifest *m, long long deadline)
{
    int a = asset_entry(m, "notes.txt"); if (a < 0) return;
    long long size = jn[jget(a, "size")].i; if (size <= 0 || size > NOTES_MAX) return;
    static char nb[NOTES_MAX + 1]; Buf b = {nb, 0, NOTES_MAX}; int ne; char want[65], got[65]; unsigned char h[32];
    char url[300], base[200];
    if (jcpy(jget(m->root, "base_url"), base, sizeof base) || jcpy(jget(a, "sha256"), want, sizeof want)) return;
    snprintf(url, sizeof url, "%snotes.txt", base);
    phase("notes");
    if (fetch_small(url, &b, size, deadline, &ne) || (long long)b.n != size) return;
    mp_sha256_init(); mp_sha256_update(nb, b.n); mp_sha256_final(h); tohex(h, 32, got);
    if (strcmp(got, want)) return;                                          /* not the signed notes: show none */
    int k = 0;
    for (char *l = nb; l && *l && k < 4; ) {
        char *e = strchr(l, '\n'); if (e) *e = 0;
        char *s = l; while (*s == ' ' || *s == '-' || *s == '*') s++;
        if (*s) {
            size_t o = 0;
            for (char *q = s; *q && o < sizeof S.notes[k] - 1; q++) {
                unsigned char c = (unsigned char)*q;
                if (c == '*' || c == '`' || c == '_') continue;                 /* markdown */
                if (c < 0x20 || c > 0x7e) c = ' ';
                if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
                S.notes[k][o++] = (char)c;
            }
            S.notes[k][o] = 0; k++;
        }
        l = e ? e + 1 : NULL;
    }
}

static int run_lock(void)                  /* one updater at a time */
{
    int fd = open(P(RUNLOCK_F), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB)) { if (fd >= 0) close(fd); return -1; }
    return fd;
}
static void status_init(const char *op, const char *path)
{
    memset(&S, 0, sizeof S); S.op = op; S.state = "running"; S.path = path; S.result = ""; S.error = ""; S.blocked = "";
}

static int cmd_check(const char *status)
{
    status_init("check", status);
    if (run_lock() < 0) { fail(E_BUSY, "another update check or install is running"); return finish(E_BUSY, "busy"); }
    if (nice(10) == -1) {}
    long long deadline = mono_ms() + CHECK_MS;
    St st; state_read(&st);
    snprintf(S.current, sizeof S.current, "%s", st.cur);
    Manifest m; const char *kind = "net";
    int e = fetch_manifest(&m, deadline, &kind);
    if (e && !strcmp(kind, "none")) { S.result = "none"; return finish(E_OK, ""); }     /* no signed release published (404) */
    if (e) return finish(e, kind);
    char cv[16]; rel_version(st.cur, cv, sizeof cv);
    if (m.seq < st.max_seq) { fail(E_OLD, "OLDER VERSION REFUSED: seq %lld < %lld", m.seq, st.max_seq); return finish(E_OLD, "older"); }
    if (m.seq == st.max_seq || !strcmp(cv, m.version)) { S.result = "current"; return finish(E_OK, ""); }
    if (rel_entry(&m, "demo_v9") < 0) { fail(E_FMT, "the release has no UI"); return finish(E_FMT, "format"); }
    S.result = "available";
    if (!launcher_ok(&m)) S.blocked = "launcher";
    fetch_notes(&m, deadline);
    return finish(E_OK, "");
}

/* ---- staging ---- */
typedef struct { int fd; long long got, size; } FileSink;
static long long ins_done, ins_total;
static int file_sink(void *c, const unsigned char *p, size_t n)
{
    FileSink *f = c;
    if (f->got + (long long)n > f->size) return 1;
    size_t o = 0; while (o < n) { ssize_t w = write(f->fd, p + o, n - o); if (w <= 0) return 1; o += (size_t)w; }
    mp_sha256_update(p, n); f->got += (long long)n;
    return 0;
}
static void ins_progress(void *c, long long got, long long total)
{
    (void)total; long long *base = c;
    S.done = *base + got; S.pct = ins_total ? (int)(S.done * 100 / ins_total) : 0;
    if (mono_ms() - S.t_last >= 400) status_write();
}
static void rmtree(const char *path, int depth)
{
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e; char p[700];
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(p, sizeof p, "%s/%s", path, e->d_name);
            struct stat sb;
            if (!lstat(p, &sb) && S_ISDIR(sb.st_mode) && depth < 4) rmtree(p, depth + 1); else unlink(p);
        }
        closedir(d);
    }
    rmdir(path);
}
static int name_ok(const char *n)
{
    if (!n[0] || n[0] == '.' || strlen(n) >= 64) return 0;
    for (const char *p = n; *p; p++) if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '.' || *p == '-' || *p == '_')) return 0;
    return 1;
}
/* remove what nothing references: our own interrupted stages, moved-aside damaged releases, and release directories
 * that are neither current, previous, pending, running nor the one being installed (the installer does the same).
 * Under ota/lock with the state re-read there, and never a directory changed in the last GC_MIN_AGE seconds: a computer
 * install (tools/merakiplus.py) that is filling rel/.stage-<x> or has just renamed rel/<x> (state not yet written) is
 * left alone. A clock far behind the files' times (reset RTC) counts as old, so space is still reclaimed. */
#define GC_MIN_AGE 600
static void gc(const char *running, const char *keep)
{
    int lk = ota_lock(); if (lk < 0) return;                               /* busy: collect next time */
    St stl; state_read(&stl); const St *st = &stl;
    if (!stl.ok) { close(lk); return; }                                 /* no state: nothing is known to be unreferenced */
    DIR *d = opendir(P(RELDIR)); if (!d) { close(lk); return; }
    struct dirent *e; char p[700]; int n = 0; long long now = (long long)time(NULL);
    while ((e = readdir(d))) {
        const char *x = e->d_name;
        if (!strcmp(x, ".") || !strcmp(x, "..")) continue;
        int junk = !strncmp(x, ".stage-", 7) || !strncmp(x, ".damaged-", 9);
        int old = name_ok(x) && strcmp(x, st->cur) && strcmp(x, st->prev) && strcmp(x, st->pend) && strcmp(x, running) && strcmp(x, keep);
        if (!junk && !old) continue;
        snprintf(p, sizeof p, "%s/%s", P(RELDIR), x);
        struct stat sb; if (lstat(p, &sb) || !S_ISDIR(sb.st_mode)) continue;
        long long age = now - (long long)sb.st_mtime; if (age > -60 && age < GC_MIN_AGE) continue;
        rmtree(p, 0); n++;
    }
    closedir(d);
    if (n) { sync(); fsync_dir(P(RELDIR "/x")); }
    close(lk);
}
static int rel_complete(const Manifest *m, const char *dir)                /* every release file matches + a MANIFEST */
{
    char p[700]; snprintf(p, sizeof p, "%s/MANIFEST", dir);
    if (access(p, R_OK)) return 0;
    int cnt = 0; return check_files(m->release, dir, &cnt) == E_OK;
}
static int mkdirs_for(const char *dir, const char *rel)                   /* parents of rel (audio/) inside dir */
{
    char p[700]; snprintf(p, sizeof p, "%s/%s", dir, rel);
    for (char *s = p + strlen(dir) + 1; (s = strchr(s, '/')); s++) { *s = 0; if (mkdir(p, 0755) && errno != EEXIST) return -1; *s = '/'; }
    return 0;
}
static int stage_release(const Manifest *m, const char *stage, long long deadline, const char **kind)
{
    char base[200]; jcpy(jget(m->root, "base_url"), base, sizeof base);
    rmtree(stage, 0);
    if (mkdir(stage, 0755)) { *kind = "io"; return fail(E_IO, "cannot create %s (%s)", stage, strerror(errno)); }
    static char mf[8192]; int mn = snprintf(mf, sizeof mf, "merakiplus-release 1\nversion %s\n", m->version);
    ins_done = 0; S.done = 0; S.pct = 0;
    for (int e = jn[m->release].child; e >= 0; e = jn[e].next) {
        char name[160], src[96], want[65], got[65], mode[8], url[400], path[700]; unsigned char h[32];
        long long size = jn[jget(e, "size")].i;
        if (jcpy(jget(e, "name"), name, sizeof name) || jcpy(jget(e, "src"), src, sizeof src) ||
            jcpy(jget(e, "sha256"), want, sizeof want) || jcpy(jget(e, "mode"), mode, sizeof mode)) { *kind = "format"; return fail(E_FMT, "manifest entry"); }
        if (!safe_name(src, strlen(src))) { *kind = "format"; return fail(E_FMT, "bad asset name"); }
        if (mkdirs_for(stage, name)) { *kind = "io"; return fail(E_IO, "mkdir for %s (%s)", name, strerror(errno)); }
        snprintf(path, sizeof path, "%s/%s", stage, name);
        snprintf(url, sizeof url, "%s%s", base, src);
        FileSink fs = {open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600), 0, size};
        if (fs.fd < 0) { *kind = "io"; return fail(E_IO, "create %s (%s)", name, strerror(errno)); }
        mp_http hh; memset(&hh, 0, sizeof hh); hh.ua = UA; hh.deadline = deadline; hh.limit = size;
        hh.progress = ins_progress; hh.pctx = &ins_done;
        mp_sha256_init();
        int r = mp_https_get(&hh, url, file_sink, &fs);
        if (r) {
            close(fs.fd);
            if (r == NE_SINK && fs.got + 1 > size) { *kind = "asset"; return fail(E_ASSET, "%s is larger than its signed size", name); }
            if (r == NE_SINK) { *kind = "io"; return fail(E_IO, "writing %s: %s", name, strerror(errno ? errno : EIO)); }
            if (r == NE_LIMIT) { *kind = "asset"; return fail(E_ASSET, "%s is larger than its signed size", name); }
            *kind = net_kind(r); snprintf(emsg, sizeof emsg, "%s", hh.err); return E_NET;
        }
        mp_sha256_final(h); tohex(h, 32, got);
        if (fs.got != size || strcmp(got, want)) { close(fs.fd); *kind = "asset"; return fail(E_ASSET, "%s does not match the signed manifest", name); }
        if (fsync(fs.fd) || fchmod(fs.fd, !strcmp(mode, "755") ? 0755 : 0644)) { close(fs.fd); *kind = "io"; return fail(E_IO, "fsync %s (%s)", name, strerror(errno)); }
        close(fs.fd);
        long long sz2; unsigned char h2[32]; char got2[65];                      /* re-read from flash (§9: bit rot / write errors) */
        if (hash_file(path, &sz2, h2) || (tohex(h2, 32, got2), sz2 != size || strcmp(got2, want))) { *kind = "asset"; return fail(E_ASSET, "%s changed after writing (flash error?)", name); }
        ins_done += size;
        mn += snprintf(mf + mn, sizeof mf - (size_t)mn, "file %s %lld %s\n", name, size, want);
        if (mn >= (int)sizeof mf) { *kind = "format"; return fail(E_FMT, "too many files"); }
    }
    char mp_[700]; snprintf(mp_, sizeof mp_, "%s/MANIFEST", stage);
    int fd = open(mp_, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0 || write(fd, mf, (size_t)mn) != mn || fsync(fd)) { if (fd >= 0) close(fd); *kind = "io"; return fail(E_IO, "MANIFEST (%s)", strerror(errno)); }
    close(fd);
    S.pct = 100; S.done = ins_total;
    return E_OK;
}

static int cmd_install(const char *status, const char *want_ver, const char *running)
{
    status_init("install", status);
    if (run_lock() < 0) { fail(E_BUSY, "another update check or install is running"); return finish(E_BUSY, "busy"); }
    if (nice(10) == -1) {}
    long long deadline = mono_ms() + INSTALL_MS;
    St st; state_read(&st);
    snprintf(S.current, sizeof S.current, "%s", st.cur);
    if (!st.ok) { fail(E_BUSY, "no release state (install from a computer first)"); return finish(E_BUSY, "launcher"); }
    Manifest m; const char *kind = "net";
    int e = fetch_manifest(&m, deadline, &kind);
    if (e && !strcmp(kind, "none")) { fail(E_FMT, "no signed release is published"); return finish(E_FMT, "changed"); }
    if (e) return finish(e, kind);
    if (strcmp(m.version, want_ver)) { fail(E_FMT, "the latest release is v%s now, not v%s", m.version, want_ver); return finish(E_FMT, "changed"); }
    int ui = rel_entry(&m, "demo_v9");
    if (ui < 0) { fail(E_FMT, "the release has no UI"); return finish(E_FMT, "format"); }
    char hx[65]; jcpy(jget(ui, "sha256"), hx, sizeof hx);
    snprintf(S.name, sizeof S.name, "%s-%.8s", m.version, hx);
    int again = st.pend[0] && !strcmp(st.pend, S.name);                    /* staged before (AT NEXT START): idempotent */
    if (st.pend[0] && !again) { fail(E_BUSY, "release %s is on trial: wait until it is committed or rolled back", st.pend); return finish(E_BUSY, "trial"); }
    if (!again && m.seq <= st.max_seq) { fail(E_OLD, "OLDER VERSION REFUSED: seq %lld <= %lld", m.seq, st.max_seq); return finish(E_OLD, "older"); }
    char cv[16]; rel_version(st.cur, cv, sizeof cv);
    if (!strcmp(cv, m.version)) { fail(E_OLD, "v%s is already installed", m.version); return finish(E_OLD, "older"); }
    if (!launcher_ok(&m)) { fail(E_BUSY, "this release needs a newer launcher: install it from a computer"); return finish(E_BUSY, "launcher"); }
    char target[640], stage[640];
    snprintf(target, sizeof target, "%s/%s", P(RELDIR), S.name);
    snprintf(stage, sizeof stage, "%s/.stage-%s", P(RELDIR), S.name);
    mkdir(P(OURDIR "/rel"), 0755); mkdir(P(OURDIR "/ota"), 0755);
    phase("space");
    gc(running ? running : "", S.name);
    ins_total = 0;
    for (int x = jn[m.release].child; x >= 0; x = jn[x].next) ins_total += jn[jget(x, "size")].i;
    S.total = ins_total;
    struct stat sb;
    if (!stat(target, &sb) && rel_complete(&m, target)) {
        S.done = ins_total; S.pct = 100;                                      /* already staged earlier (AT NEXT START, retry) */
    } else {
        if (!stat(target, &sb)) {                                             /* damaged: moved aside, staged again */
            char aside[700]; snprintf(aside, sizeof aside, "%s/.damaged-%s-%d", P(RELDIR), S.name, (int)getpid());
            if (rename(target, aside)) { fail(E_IO, "cannot move the damaged %s aside", S.name); return finish(E_IO, "io"); }
        }
        struct statvfs vf;
        long long need = 2 * ins_total + 1024 * 1024, avail = statvfs(P(RELDIR), &vf) ? -1 : (long long)vf.f_bavail * (long long)vf.f_frsize;
#ifdef MERAKIPLUS_UPD_TEST
        const char *fk = getenv("MERAKIPLUS_UPD_TEST_FREE"); if (fk) avail = atoll(fk);
#endif
        if (avail >= 0 && avail < need) { fail(E_IO, "only %lld KB free, need %lld KB", avail / 1024, need / 1024); return finish(E_IO, "space"); }
        phase("download");
        if ((e = stage_release(&m, stage, deadline, &kind))) { rmtree(stage, 0); return finish(e, kind); }
        phase("stage");
        sync();
        if (rename(stage, target)) { rmtree(stage, 0); fail(E_IO, "rename into rel/ failed (%s)", strerror(errno)); return finish(E_IO, "io"); }
        fsync_dir(target);
    }
    /* the commit point: pending=<name> + trial_seq=<name> <seq> under ota/lock. max_seq is NOT raised here: the seq
     * counts once the launcher commits the release as healthy (state_read), so a rolled-back trial can be retried */
    phase("commit");
    int lk = ota_lock();
    if (lk < 0) { fail(E_BUSY, "ota/lock is held (the launcher or an installer is busy)"); return finish(E_BUSY, "busy"); }
    St now; state_read(&now);
    if (!now.ok || strcmp(now.cur, st.cur) || (now.pend[0] && strcmp(now.pend, S.name))) {
        close(lk); fail(E_BUSY, "the release state changed while downloading"); return finish(E_BUSY, "trial");
    }
    snprintf(now.pend, sizeof now.pend, "%s", S.name); now.tries = 0;
    snprintf(now.trial, sizeof now.trial, "%s", S.name); now.trial_seq = m.seq;   /* (max_seq keeps an earlier committed trial) */
    unlink(P(HEALTHY_F));
    int w = state_write(&now);
    close(lk);
    if (w) { fail(E_IO, "cannot write ota/state (%s)", strerror(errno)); return finish(E_IO, "io"); }
    S.result = "staged";
    return finish(E_OK, "");
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
    /* phase 3: URL rules (HTTPS, pinned hosts, no port/userinfo), the certificate clock floor */
    char hh[128], pp[256];
    T("url: discovery URL parses", !mp_url_parse(DISCOVERY "manifest.json", hh, sizeof hh, pp, sizeof pp) && !strcmp(hh, "github.com") && mp_host_pinned(hh));
    T("url: http:// refused", mp_url_parse("http://github.com/x", hh, sizeof hh, pp, sizeof pp) != 0);
    T("url: port / userinfo / upper case refused", mp_url_parse("https://github.com:8443/x", hh, sizeof hh, pp, sizeof pp) != 0 &&
      mp_url_parse("https://a@github.com/x", hh, sizeof hh, pp, sizeof pp) != 0 && mp_url_parse("https://GitHub.com/x", hh, sizeof hh, pp, sizeof pp) != 0);
    T("url: pinned hosts only", mp_host_pinned("release-assets.githubusercontent.com") && mp_host_pinned("objects.githubusercontent.com") &&
      !mp_host_pinned("api.github.com") && !mp_host_pinned("github.com.evil.example") && !mp_host_pinned("raw.githubusercontent.com"));
    int fl1, fl2, fl3; long long c1 = mp_cert_time(0, &fl1), c2 = mp_cert_time(1900000000LL, &fl2), c3 = mp_cert_time(9000000000LL, &fl3);
    T("clock floor: a 1970 clock uses the floor, a sane clock is kept, a far-future clock uses the floor",
      fl1 && c1 > 1790000000LL && !fl2 && c2 == 1900000000LL && fl3 && c3 == c1);
    printf("merakiplus_upd selftest: %d/%d passed\n", n - bad, n);
    return bad ? 1 : 0;
}

/* the third-party notices (BearSSL, Zig std, musl — all MIT) travel inside the binary that contains BearSSL, so every copy
 * carries them, also one fetched by the updater itself: `merakiplus_upd notices` (the same file is a release asset) */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static const char NOTICES[] = {
#embed "../THIRD-PARTY-NOTICES"
    , 0};
#pragma clang diagnostic pop

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--version")) { printf("%s\n", &MARK[4]); return 0; }
    if (argc == 2 && !strcmp(argv[1], "notices")) { fputs(NOTICES, stdout); return 0; }
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
    if (argc >= 2 && (!strcmp(argv[1], "check") || !strcmp(argv[1], "install"))) {
#ifdef MERAKIPLUS_UPD_TEST
        const char *r = getenv("MERAKIPLUS_UPD_ROOT"); if (r) snprintf(root, sizeof root, "%s", r);
#endif
        const char *status = P(STATUS_F), *ver = 0, *running = 0;
        static char st_path[600]; snprintf(st_path, sizeof st_path, "%s", status); status = st_path;
        for (int i = 2; i < argc; i++) {
            if (i + 1 >= argc) goto usage;
            if (!strcmp(argv[i], "--status")) status = argv[++i];
            else if (!strcmp(argv[i], "--version")) ver = argv[++i];
            else if (!strcmp(argv[i], "--running")) running = argv[++i];
            else goto usage;
        }
        setvbuf(stdout, NULL, _IOLBF, 0);
        signal(SIGPIPE, SIG_IGN);              /* a reset connection is an error to report (EPIPE), never a silent death */
        if (argv[1][0] == 'c') return cmd_check(status);
        if (!ver || !is_version(ver)) goto usage;
        return cmd_install(status, ver, running);
    }
usage:
    fprintf(stderr, "usage: merakiplus_upd --version | --selftest | keys | notices | sha256 FILE...\n"
                    "       merakiplus_upd verify MANIFEST SIG [--max-seq N] [--version V] [--assets DIR] [--release-dir DIR]\n"
                    "       merakiplus_upd check [--status FILE]\n"
                    "       merakiplus_upd install --version V [--status FILE] [--running NAME]\n");
    return E_USAGE;
}
