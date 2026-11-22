/* merakiplus_boot.c — the Meraki+ launcher (docs/ota-plan.md §5.4, phase 1b).
 *
 * Installed as /new_customer/demo_v9: the file the vendor watchdog (/customer/test.sh new_customer)
 * restarts whenever no "demo_v9" process exists, and the file the gesture switcher starts. It picks a
 * release directory /new_customer/merakiplus/rel/<name>/ and exec()s its demo_v9 (same PID, so the
 * watchdog never sees a gap). It has no UI, no network, no UART and never sends a signal.
 *
 * Choice, on every start:
 *   1. pending (a release on trial, written by the installer) — if it already wrote its healthy marker
 *      (ota/healthy), it is committed: current=<it>, previous=<old current>. Otherwise tries+1 is
 *      written to flash BEFORE the exec; at the 4th start without healthy (3 failed starts) or when it
 *      is damaged it is rolled back: pending cleared, last=rolled_back:<name>:<reason>.
 *   2. steady state (no flash write): current -> previous -> STOCK UI from slot A. A level is left for the
 *      rest of this boot when its release is damaged (size/SHA-256 vs. its MANIFEST), or after a crash
 *      storm: 3 starts in a row in this boot that never became healthy, or a 7th start within 10 min.
 *      The per-boot counters live in /tmp (tmpfs: a power cut resets them) and carry the boot_id.
 *   3. stock: chdir /customer, slot A's LD_LIBRARY_PATH, execv /customer/demo_v9 (argv0 ./demo_v9).
 *      Slot A is executed, never written; the boot flag /customer/new is never touched. The stock UI
 *      brings Wi-Fi up, so a Meraki+ build that crashes before its own Wi-Fi start cannot soft-brick.
 * An explicit start (MERAKIPLUS_LAUNCH=gesture|reload|install) resets this boot's fallback level.
 * The UI gets MERAKIPLUS_REL (its directory), MERAKIPLUS_RELNAME, MERAKIPLUS_TRIAL=1 on trial, MERAKIPLUS_FALLBACK=1 when an
 * older level runs this boot, and MERAKIPLUS_NOTE=<release> when a release failed right before this start (one-time notice).
 *
 * Healthy: the UI writes /tmp/merakiplus_healthy ("boot=<boot_id> pid=<pid> rel=<name>") once it has run
 * 60 s with a talking controller, or when it exits on purpose; on trial it also writes ota/healthy
 * (flash, tmp + fsync + rename). Matching boot_id + PID means "the previous start in this boot was fine".
 *
 * Power cuts: every flash write is tmp -> fsync -> rename -> fsync(dir); a cut leaves the old or the new
 * state, never a mix (tests/test_launcher.py replays the operation log at every point).
 *
 * Usage: demo_v9                      (normal: decide + exec)
 *        demo_v9 --dry                (print the decision, write nothing, exec nothing)
 *        demo_v9 --selftest <dir>     (SHA-256 vectors + state round trip in <dir>; run by the installer
 *                                      on the display before the launcher is renamed into place)
 *        demo_v9 --version
 * Test env: MERAKIPLUS_BOOT_ROOT=<dir> prefixes every path (/new_customer, /customer, /tmp, /proc);
 *           MERAKIPLUS_BOOT_OPLOG=<file> appends every file operation (power-cut replay).
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>

#define LAUNCHER_VERSION "1"
/* "@(#)merakiplus " = the marker every Meraki+ binary carries (installer, UI BOOT row, older UIs);
 * "@(#)merakiplus-launcher " tells the installer this is the launcher, not a UI. */
static const char MARK[] = "@(#)merakiplus launcher\n@(#)merakiplus-launcher " LAUNCHER_VERSION;

#define OURDIR    "/new_customer/merakiplus"
#define RELDIR    OURDIR "/rel"
#define OTADIR    OURDIR "/ota"
#define STATE     OTADIR "/state"
#define HEALTHY_P OTADIR "/healthy"
#define LOCKF     OTADIR "/lock"
#define LOGF      OTADIR "/launch.log"
#define VOLF      "/tmp/merakiplus_launch"
#define HEALTHY_V "/tmp/merakiplus_healthy"
#define BOOTID    "/proc/sys/kernel/random/boot_id"
#define UPTIME    "/proc/uptime"
#define STOCK_DIR "/customer"
#define STOCK_EXE "/customer/demo_v9"
#define MAX_TRIES   3          /* failed trial starts before a rollback */
#define MAX_STREAK  3          /* failed starts in a row (this boot) before the next fallback level */
#define WIN_N       6          /* a 7th start ... */
#define WIN_S     600          /* ... within 10 min = crash storm */
#define NAME_MAX_ 64

static char root[256] = "";
static char note[NAME_MAX_] = "";          /* a release that failed right before this start (-> MERAKIPLUS_NOTE, a one-time notice) */
static FILE *oplog = NULL;
static int dry = 0;

static void P(char *o, size_t n, const char *path) { snprintf(o, n, "%s%s", root, path); }

/* ------------------------------------------------------------ operation layer (logged for replay) */
static void op(const char *kind, const char *path, const char *data, size_t n)
{
    if (!oplog) return;
    fprintf(oplog, "%s %s", kind, path + strlen(root));
    if (data) { fputc(' ', oplog); for (size_t i = 0; i < n; i++) fprintf(oplog, "%02x", (unsigned char)data[i]); }
    fputc('\n', oplog);
    fflush(oplog);
}
static int fsync_dir(const char *path)
{
    char d[512]; snprintf(d, sizeof d, "%s", path);
    char *s = strrchr(d, '/'); if (!s) return -1; if (s == d) s[1] = 0; else *s = 0;
    int fd = open(d, O_RDONLY | O_DIRECTORY | O_CLOEXEC); if (fd < 0) return -1;
    int r = fsync(fd); close(fd); op("D", d, NULL, 0); return r;
}
/* the only way the launcher writes a durable file: tmp -> fsync -> rename -> fsync(dir) */
static int put_atomic(const char *path, const char *data, size_t n, int durable)
{
    char tmp[520]; snprintf(tmp, sizeof tmp, "%s.tmp.L", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644); if (fd < 0) return -1;
    size_t o = 0; while (o < n) { ssize_t w = write(fd, data + o, n - o); if (w <= 0) { close(fd); unlink(tmp); return -1; } o += (size_t)w; }
    op("W", tmp, data, n);
    if (durable) { fsync(fd); op("F", tmp, NULL, 0); }
    close(fd);
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    op("R", tmp, path, strlen(path));
    if (durable) fsync_dir(path);
    return 0;
}
static int slurp(const char *path, char *b, size_t n)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC); if (fd < 0) { b[0] = 0; return -1; }
    size_t o = 0; ssize_t r; while (o + 1 < n && (r = read(fd, b + o, n - 1 - o)) > 0) o += (size_t)r;
    close(fd); b[o] = 0; return (int)o;
}
/* value of "key=" in a key=value text (first match at a line start), "" when absent */
static void kv(const char *txt, const char *key, char *o, size_t n)
{
    size_t kl = strlen(key); o[0] = 0;
    for (const char *p = txt; p && *p; ) {
        if (!strncmp(p, key, kl) && p[kl] == '=') { p += kl + 1; size_t l = strcspn(p, "\n"); if (l >= n) l = n - 1; memcpy(o, p, l); o[l] = 0; return; }
        p = strchr(p, '\n'); if (p) p++;
    }
}

/* ------------------------------------------------------------ SHA-256 (FIPS 180-4) */
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t n; } Sha;
static const uint32_t K256[64] = {
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
 0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
 0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
 0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_blk(Sha *s, const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, h;
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3), s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha_init(Sha *s)
{
    static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(s->h, iv, sizeof iv); s->len = 0; s->n = 0;
}
static void sha_add(Sha *s, const uint8_t *p, size_t n)
{
    s->len += n;
    while (n) { size_t k = 64 - s->n; if (k > n) k = n; memcpy(s->buf + s->n, p, k); s->n += k; p += k; n -= k; if (s->n == 64) { sha_blk(s, s->buf); s->n = 0; } }
}
static void sha_hex(Sha *s, char out[65])
{
    uint64_t bits = s->len * 8; uint8_t z = 0x80; sha_add(s, &z, 1); z = 0;
    while (s->n != 56) sha_add(s, &z, 1);
    uint8_t L[8]; for (int i = 0; i < 8; i++) L[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha_add(s, L, 8);
    for (int i = 0; i < 8; i++) snprintf(out + 8 * i, 9, "%08x", s->h[i]);
}
static int sha_file(const char *path, char out[65], long *size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC); if (fd < 0) return -1;
    Sha s; sha_init(&s); uint8_t b[8192]; ssize_t r; long sz = 0;
    while ((r = read(fd, b, sizeof b)) > 0) { sha_add(&s, b, (size_t)r); sz += r; }
    close(fd); if (r < 0) return -1;
    sha_hex(&s, out); *size = sz; return 0;
}

/* ------------------------------------------------------------ releases */
static int name_ok(const char *n)
{
    if (!n[0] || n[0] == '.' || strlen(n) >= NAME_MAX_) return 0;
    for (const char *p = n; *p; p++) if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-' && *p != '_') return 0;
    return 1;
}
/* complete and intact: rel/<n>/demo_v9 is an executable regular file whose size + SHA-256 match the line
 * "file demo_v9 <size> <sha256>" in rel/<n>/MANIFEST (written last, before the directory got its name) */
static int usable(const char *n, char *why, size_t wn)
{
    char p[520], m[4096], t[NAME_MAX_ + 64];
    if (!name_ok(n)) { snprintf(why, wn, "bad name"); return 0; }
    snprintf(t, sizeof t, RELDIR "/%s/MANIFEST", n); P(p, sizeof p, t);
    if (slurp(p, m, sizeof m) <= 0) { snprintf(why, wn, "no MANIFEST"); return 0; }
    const char *l = strstr(m, "\nfile demo_v9 "); if (!l) { snprintf(why, wn, "MANIFEST without demo_v9"); return 0; }
    long want = -1; char hx[65] = "";
    if (sscanf(l + 14, "%ld %64s", &want, hx) != 2 || strlen(hx) != 64) { snprintf(why, wn, "MANIFEST unreadable"); return 0; }
    snprintf(t, sizeof t, RELDIR "/%s/demo_v9", n); P(p, sizeof p, t);
    struct stat st;
    if (stat(p, &st) != 0 || !S_ISREG(st.st_mode) || access(p, X_OK) != 0) { snprintf(why, wn, "demo_v9 missing"); return 0; }
    char got[65]; long sz;
    if (sha_file(p, got, &sz) != 0) { snprintf(why, wn, "demo_v9 unreadable"); return 0; }
    if (sz != want || strcmp(got, hx)) { snprintf(why, wn, "demo_v9 damaged (size/sha256)"); return 0; }
    return 1;
}
/* numeric-aware compare ("6.9" < "6.21") for the no-state-file fallback */
static int vcmp(const char *a, const char *b)
{
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
            if (x != y) return x < y ? -1 : 1;
        } else { if (*a != *b) return (unsigned char)*a < (unsigned char)*b ? -1 : 1; a++; b++; }
    }
    return *a ? 1 : (*b ? -1 : 0);
}
static void newest_usable(char *o, size_t n)
{
    char p[520], why[64]; o[0] = 0; P(p, sizeof p, RELDIR);
    DIR *d = opendir(p); if (!d) return; struct dirent *e;
    while ((e = readdir(d))) if (name_ok(e->d_name) && (!o[0] || vcmp(e->d_name, o) > 0) && usable(e->d_name, why, sizeof why)) snprintf(o, n, "%s", e->d_name);
    closedir(d);
}

/* ------------------------------------------------------------ state */
typedef struct {
    int ok;                                    /* parsed (format=1) */
    char cur[NAME_MAX_], prev[NAME_MAX_], pend[NAME_MAX_], last[160];
    int tries;
    char extra[1024];                          /* unknown lines, kept as they are (newer writers) */
} State;
static void state_read(State *s)
{
    char p[520], b[4096]; memset(s, 0, sizeof *s); P(p, sizeof p, STATE);
    if (slurp(p, b, sizeof b) <= 0) return;
    char f[16]; kv(b, "format", f, sizeof f); if (strcmp(f, "1")) return;
    kv(b, "current", s->cur, sizeof s->cur); kv(b, "previous", s->prev, sizeof s->prev);
    kv(b, "pending", s->pend, sizeof s->pend); kv(b, "last", s->last, sizeof s->last);
    char t[16]; kv(b, "tries", t, sizeof t); s->tries = atoi(t);
    static const char *known[] = {"format", "current", "previous", "pending", "tries", "last"};
    size_t o = 0;
    for (char *l = b; l && *l; ) {
        char *e = strchr(l, '\n'); size_t ll = e ? (size_t)(e - l) : strlen(l); int k = 0;
        for (unsigned i = 0; i < 6; i++) { size_t kl = strlen(known[i]); if (ll > kl && !strncmp(l, known[i], kl) && l[kl] == '=') k = 1; }
        if (!k && ll && memchr(l, '=', ll) && o + ll + 2 < sizeof s->extra) { memcpy(s->extra + o, l, ll); o += ll; s->extra[o++] = '\n'; s->extra[o] = 0; }
        l = e ? e + 1 : NULL;
    }
    s->ok = 1;
}
static int state_write(const State *s)
{
    char b[2048], p[520];
    int n = snprintf(b, sizeof b, "format=1\ncurrent=%s\nprevious=%s\npending=%s\ntries=%d\nlast=%s\n%s",
                     s->cur, s->prev, s->pend, s->tries, s->last, s->extra);
    if (n <= 0 || n >= (int)sizeof b) return -1;
    if (dry) return 0;
    P(p, sizeof p, STATE); return put_atomic(p, b, (size_t)n, 1);
}

/* ------------------------------------------------------------ this boot (volatile, /tmp) */
typedef struct { char boot[64], rel[NAME_MAX_]; long pid; int streak, level, nw; long w[WIN_N]; } Vol;
static char boot_id[64] = "?";
static long uptime_s(void)
{
    char p[520], b[64]; P(p, sizeof p, UPTIME);
    return slurp(p, b, sizeof b) > 0 ? atol(b) : 0;
}
static void vol_read(Vol *v)
{
    char p[520], b[1024], t[160]; memset(v, 0, sizeof *v); P(p, sizeof p, VOLF);
    if (slurp(p, b, sizeof b) <= 0) return;
    kv(b, "boot", v->boot, sizeof v->boot);
    if (strcmp(v->boot, boot_id)) { memset(v, 0, sizeof *v); return; }     /* another boot: start afresh */
    kv(b, "rel", v->rel, sizeof v->rel);
    kv(b, "pid", t, sizeof t); v->pid = atol(t);
    kv(b, "streak", t, sizeof t); v->streak = atoi(t);
    kv(b, "level", t, sizeof t); v->level = atoi(t);
    kv(b, "w", t, sizeof t);
    for (char *q = t; *q && v->nw < WIN_N; ) { v->w[v->nw++] = strtol(q, &q, 10); if (*q == ',') q++; else break; }
}
static void vol_write(const Vol *v)
{
    char b[512], p[520]; int n = snprintf(b, sizeof b, "boot=%s\nrel=%s\npid=%ld\nstreak=%d\nlevel=%d\nw=", boot_id, v->rel, v->pid, v->streak, v->level);
    for (int i = 0; i < v->nw && n < (int)sizeof b - 16; i++) n += snprintf(b + n, sizeof b - n, i ? ",%ld" : "%ld", v->w[i]);
    n += snprintf(b + n, sizeof b - n, "\n");
    if (dry) return;
    P(p, sizeof p, VOLF); put_atomic(p, b, (size_t)n, 0);                 /* tmpfs: nothing to fsync */
}
static void logev(const char *fmt, const char *a, const char *b)
{
    char line[300], p[520], p1[530]; struct stat st;
    int n = snprintf(line, sizeof line, "boot=%.8s up=%ld ", boot_id, uptime_s());
    n += snprintf(line + n, sizeof line - n, fmt, a ? a : "", b ? b : "");
    fprintf(stderr, "merakiplus_boot: %s\n", line);
    if (dry) { printf("event: %s\n", line); return; }
    P(p, sizeof p, LOGF);
    if (stat(p, &st) == 0 && st.st_size > 16384) { snprintf(p1, sizeof p1, "%s.1", p); rename(p, p1); }
    int fd = open(p, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd >= 0) { line[sizeof line - 2] = 0; strcat(line, "\n"); if (write(fd, line, strlen(line)) < 0) {} op("A", p, line, strlen(line)); close(fd); }
}

/* ------------------------------------------------------------ exec */
/* slot A's LD_LIBRARY_PATH: the inherited prefix without the slot components + "/customer/libs:/config/wifi"
 * (the same rule as the UI's SWITCH TO ORIGINAL NOW, docs/boot.md §9.2) */
static void ld_for_a(char *out, size_t n)
{
    const char *inh = getenv("LD_LIBRARY_PATH"); size_t o = 0; out[0] = 0;
    static const char *drop[] = {"/new_customer/libs", "/customer/libs", "/config/wifi"};
    for (const char *p = inh; p && *p; ) {
        const char *e = strchr(p, ':'); size_t l = e ? (size_t)(e - p) : strlen(p); int skip = l == 0;
        for (unsigned k = 0; k < 3 && !skip; k++) if (l == strlen(drop[k]) && !memcmp(p, drop[k], l)) skip = 1;
        if (!skip && o + l + 40 < n) { memcpy(out + o, p, l); o += l; out[o++] = ':'; out[o] = 0; }
        p = e ? e + 1 : NULL;
    }
    snprintf(out + o, n - o, "/customer/libs:/config/wifi");
}
static void clean_env(void)
{
    unsetenv("MERAKIPLUS_REL"); unsetenv("MERAKIPLUS_RELNAME"); unsetenv("MERAKIPLUS_TRIAL"); unsetenv("MERAKIPLUS_LAUNCH");
    unsetenv("MERAKIPLUS_FALLBACK"); unsetenv("MERAKIPLUS_NOTE");
}
static void exec_rel(const char *n, int trial, int fallback)
{
    char d[520], e[540], t[NAME_MAX_ + 32];
    snprintf(t, sizeof t, RELDIR "/%s", n); P(d, sizeof d, t); snprintf(e, sizeof e, "%s/demo_v9", d);
    if (dry) { printf("exec %s trial=%d\n", e + strlen(root), trial); exit(0); }
    clean_env();
    setenv("MERAKIPLUS_REL", d, 1); setenv("MERAKIPLUS_RELNAME", n, 1);
    if (trial) setenv("MERAKIPLUS_TRIAL", "1", 1);
    if (fallback) setenv("MERAKIPLUS_FALLBACK", "1", 1);
    if (note[0]) setenv("MERAKIPLUS_NOTE", note, 1);
    if (chdir(d) != 0) return;
    char *const av[] = {"./demo_v9", NULL};
    execv(e, av);                                         /* returns only on failure */
}
static void exec_stock(void)
{
    char d[520], e[520], ld[1024]; P(d, sizeof d, STOCK_DIR); P(e, sizeof e, STOCK_EXE);
    if (dry) { printf("exec %s trial=0\n", STOCK_EXE); exit(0); }
    clean_env(); ld_for_a(ld, sizeof ld); setenv("LD_LIBRARY_PATH", ld, 1);
    if (chdir(d) != 0) return;
    char *const av[] = {"./demo_v9", NULL};
    execv(e, av);
}

/* ------------------------------------------------------------ selftest (run on the display before the swap) */
static int selftest(const char *dir)
{
    int ok = 0, n = 0; char h[65];
    struct { const char *m; const char *x; } v[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"}};
    for (unsigned i = 0; i < 3; i++) { Sha s; sha_init(&s); sha_add(&s, (const uint8_t *)v[i].m, strlen(v[i].m)); sha_hex(&s, h); n++; ok += !strcmp(h, v[i].x); }
    { Sha s; sha_init(&s); uint8_t a[1000]; memset(a, 'a', sizeof a); for (int i = 0; i < 1000; i++) sha_add(&s, a, sizeof a);
      sha_hex(&s, h); n++; ok += !strcmp(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"); }
    n++; ok += name_ok("6.21-42cd7b9d") && !name_ok(".stage-x") && !name_ok("a/b") && !name_ok("..") && !name_ok("");
    n++; ok += vcmp("6.9", "6.21") < 0 && vcmp("6.21-a", "6.20-b") > 0 && vcmp("6.21", "6.21") == 0;
    /* state round trip through the real atomic writer, inside <dir> */
    snprintf(root, sizeof root, "%s", dir ? dir : "");
    char p[520]; P(p, sizeof p, OURDIR); mkdir(root, 0755);
    char q[520]; snprintf(q, sizeof q, "%s/new_customer", root); mkdir(q, 0755); mkdir(p, 0755); P(p, sizeof p, OTADIR); mkdir(p, 0755);
    State s; memset(&s, 0, sizeof s); snprintf(s.cur, sizeof s.cur, "6.21-x"); snprintf(s.prev, sizeof s.prev, "6.20-y");
    snprintf(s.last, sizeof s.last, "ok"); s.tries = 2; snprintf(s.extra, sizeof s.extra, "max_seq=27\n");
    State r; n++; ok += state_write(&s) == 0 && (state_read(&r), r.ok && !strcmp(r.cur, "6.21-x") && !strcmp(r.prev, "6.20-y") && !r.pend[0] && r.tries == 2 && !strcmp(r.extra, "max_seq=27\n"));
    char why[64]; n++; ok += !usable("6.21-x", why, sizeof why);
    P(p, sizeof p, STATE); unlink(p); P(p, sizeof p, OTADIR); rmdir(p); P(p, sizeof p, OURDIR); rmdir(p); rmdir(q); rmdir(root);
    printf("merakiplus_boot %s selftest: %d/%d %s\n", LAUNCHER_VERSION, ok, n, ok == n ? "OK" : "FAILED");
    return ok == n ? 0 : 1;
}

/* ------------------------------------------------------------ main */
int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("%s\n", MARK + 4); return 0; }
    if (argc > 2 && !strcmp(argv[1], "--selftest")) return selftest(argv[2]);
    if (argc > 1 && !strcmp(argv[1], "--dry")) dry = 1;
    const char *e;
    if ((e = getenv("MERAKIPLUS_BOOT_ROOT"))) snprintf(root, sizeof root, "%s", e);
    if ((e = getenv("MERAKIPLUS_BOOT_OPLOG")) && !dry) oplog = fopen(e, "a");
    char p[520], b[512], why[64] = "";
    P(p, sizeof p, BOOTID); if (slurp(p, b, sizeof b) > 0) { b[strcspn(b, "\n")] = 0; snprintf(boot_id, sizeof boot_id, "%s", b); }
    long up = uptime_s();

    /* serialise concurrent launchers (watchdog + switcher); never block a boot for long */
    P(p, sizeof p, LOCKF);
    int lk = dry ? -1 : open(p, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    for (int i = 0; lk >= 0 && i < 20 && flock(lk, LOCK_EX | LOCK_NB) != 0; i++) usleep(100000);

    State s; state_read(&s);
    Vol v; vol_read(&v);
    const char *why_launch = getenv("MERAKIPLUS_LAUNCH");
    if (why_launch && (!strcmp(why_launch, "gesture") || !strcmp(why_launch, "reload") || !strcmp(why_launch, "install"))) {
        v.level = 0; v.streak = 0; v.nw = 0; v.rel[0] = 0;                 /* explicit start: try from the top again */
    }
    if (v.pid > 0) {                                                       /* did the previous start in this boot become healthy? */
        char hb[64], hp[32]; P(p, sizeof p, HEALTHY_V);
        if (slurp(p, b, sizeof b) > 0) { kv(b, "boot", hb, sizeof hb); kv(b, "pid", hp, sizeof hp);
            if (!strcmp(hb, boot_id) && atol(hp) == v.pid) v.streak = 0; }
    }

    /* 1. the trial */
    const char *pick = NULL; int trial = 0, changed = 0;
    if (s.ok && s.pend[0]) {
        char hr[NAME_MAX_]; P(p, sizeof p, HEALTHY_P);
        if (slurp(p, b, sizeof b) > 0 && (kv(b, "rel", hr, sizeof hr), !strcmp(hr, s.pend))) {
            logev("commit %s (healthy) previous=%s", s.pend, s.cur);
            snprintf(s.prev, sizeof s.prev, "%s", s.cur); snprintf(s.cur, sizeof s.cur, "%s", s.pend);
            s.pend[0] = 0; s.tries = 0; snprintf(s.last, sizeof s.last, "ok:%s", s.cur); changed = 1;
        }
    }
    if (s.ok && s.pend[0]) {
        const char *reason = !usable(s.pend, why, sizeof why) ? "damaged" : (s.tries >= MAX_TRIES ? "crashloop" : NULL);
        if (reason) {
            logev("rollback %s: %s", s.pend, reason[0] == 'd' ? why : "3 starts without healthy");
            snprintf(s.last, sizeof s.last, "rolled_back:%s:%s", s.pend, reason);
            snprintf(note, sizeof note, "%s", s.pend);
            s.pend[0] = 0; s.tries = 0; changed = 1;
        } else { s.tries++; changed = 1; pick = s.pend; trial = 1; }
    }
    if (changed) state_write(&s);                                          /* tries reach flash BEFORE the exec */

    /* 2. steady state: current -> previous -> stock, per boot */
    char scan[NAME_MAX_] = "";
    const char *chain[2] = {s.cur, s.prev};
    if (!s.ok) { newest_usable(scan, sizeof scan); chain[0] = scan; chain[1] = ""; }
    if (v.level < 0 || v.level > 2) v.level = 0;
    for (;;) {
        const char *t = pick ? pick : (v.level < 2 ? chain[v.level] : "stock");
        if (!pick && v.level < 2 && (!t[0] || !usable(t, why, sizeof why))) {
            if (t[0]) { logev("%s unusable (%s)", t, why); snprintf(note, sizeof note, "%s", t); }
            v.level++; v.streak = 0; v.nw = 0; continue;
        }
        if (!strcmp(v.rel, t)) {
            int storm = v.streak >= MAX_STREAK || (v.nw >= WIN_N && up - v.w[0] < WIN_S);
            if (storm && !pick && v.level < 2) {
                logev("crash storm on %s (%s)", t, v.streak >= MAX_STREAK ? "3 starts without healthy" : "7 starts in 10 min");
                snprintf(note, sizeof note, "%s", t);
                v.level++; v.streak = 0; v.nw = 0; continue;
            }
        } else { snprintf(v.rel, sizeof v.rel, "%s", t); v.streak = 0; v.nw = 0; }
        /* record this start, then exec */
        v.streak++; if (v.nw == WIN_N) { memmove(v.w, v.w + 1, sizeof v.w[0] * (WIN_N - 1)); v.nw--; } v.w[v.nw++] = up;
        v.pid = (long)getpid();
        if (!strcmp(t, "stock")) {
            if (v.streak == 1) logev("stock UI from slot A (no usable Meraki+ release)%s%s", "", "");
            vol_write(&v); exec_stock();
            fprintf(stderr, "merakiplus_boot: cannot start the stock UI (%s)\n", strerror(errno));
            sleep(2); return 1;                                            /* the watchdog tries again */
        }
        vol_write(&v); exec_rel(t, trial, !pick && v.level > 0);
        logev("exec %s failed: %s", t, strerror(errno));
        if (pick) { pick = NULL; trial = 0; v.rel[0] = 0; continue; }       /* trial binary will not run: steady chain */
        v.level++; v.streak = 0; v.nw = 0;
    }
}
