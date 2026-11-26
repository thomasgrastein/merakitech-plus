/* merakiplus_upd_net.c — the updater's HTTPS client (docs/ota-plan.md §6.2, phase 3).
 *
 * One GET at a time over TLS 1.2 (BearSSL, third_party/bearssl, statically linked — never the vendor's libcurl/OpenSSL):
 *   - trust: only the pinned roots of src/merakiplus_upd_anchors.h (tools/gen_anchors.py from the root certificates in keys/tls/);
 *     hostname verification on; certificate time = max(device clock, MP_CLOCK_FLOOR) (a reset clock still validates);
 *   - hosts: only https://github.com/, https://release-assets.githubusercontent.com/ and
 *     https://objects.githubusercontent.com/ on port 443 — the first URL and every redirect target are checked BEFORE a
 *     connection is made; at most 5 redirects, HTTPS only;
 *   - cipher suites: ECDHE + AEAD only (ChaCha20-Poly1305, AES-GCM), so no CBC, no static RSA, no 3DES;
 *   - limits: every body has a byte limit (the manifest ≤ 64 KiB, an asset exactly its signed size), a header limit,
 *     socket timeouts and an overall deadline. Bodies are streamed to the caller's sink — nothing large in RAM.
 * Outbound only: no listening socket. TLS gives confidentiality and an early failure; authenticity comes ONLY from the
 * Ed25519 signature checked by merakiplus_upd.c, so a TLS-intercepting middlebox can at worst block an update.
 *
 * Test builds (-DMERAKIPLUS_UPD_TEST, tests/test_ota_fetch.py and the simulator only — never build.sh's device or bin/
 * output) additionally trust a CA from MERAKIPLUS_UPD_TEST_CA (PEM) and connect every pinned host to
 * MERAKIPLUS_UPD_TEST_CONNECT=<ip>:<port>; hostnames, SNI, redirects and certificate checks stay exactly as above
 * (MERAKIPLUS_UPD_TEST_IO_TIMEOUT=<s> shortens the socket timeout for the stalled-server test).
 */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "bearssl.h"
#include "merakiplus_upd_net.h"
#include "merakiplus_upd_anchors.h"

static const char *const HOSTS[] = {"github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com", 0};
#define HDR_MAX 16384
#define MAX_REDIRECTS 5
#define IO_TIMEOUT_S 20
/* URLs and the request built from them. GitHub's signed asset URLs are ~900 bytes today and their length varies per
 * download (the base64 signature is percent-escaped), so the limits keep a wide margin: a URL (and a redirect Location)
 * up to URL_MAX - 1 bytes, the request = URL + the fixed headers. A longer one is refused with a clear error. */
#define URL_MAX 4096
#define REQ_MAX (URL_MAX + 512)

static long long now_ms(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int nfail(mp_http *h, int kind, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#include <stdarg.h>
static int nfail(mp_http *h, int kind, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vsnprintf(h->err, sizeof h->err, fmt, ap); va_end(ap);
    h->kind = kind; return kind;
}

/* ------------------------------------------------------------ the clock used for certificate validity */
long long mp_cert_time(long long now, int *floored)
{
    long long t = now;
    const char *e;
#ifdef MERAKIPLUS_UPD_TEST
    if ((e = getenv("MERAKIPLUS_UPD_TEST_NOW"))) t = atoll(e);       /* tests: pretend the device clock says this */
#else
    (void)e;
#endif
    /* before the floor (a reset clock) or absurdly far after it (a garbage clock): use the floor */
    *floored = t < MP_CLOCK_FLOOR || t > MP_CLOCK_FLOOR + 20LL * 365 * 86400;
    return *floored ? MP_CLOCK_FLOOR : t;
}

/* ------------------------------------------------------------ trust anchors (+ the test CA in test builds) */
#ifdef MERAKIPLUS_UPD_TEST
static br_x509_trust_anchor tas[MP_ANCHORS_NUM + 1];
static size_t tas_n;
static unsigned char test_dn[1024], test_key[1024];
static size_t test_dn_len;
static void dn_append(void *ctx, const void *buf, size_t len)
{
    (void)ctx;
    if (test_dn_len + len <= sizeof test_dn) { memcpy(test_dn + test_dn_len, buf, len); test_dn_len += len; }
}
static int b64_der(const char *pem, unsigned char *out, size_t cap)   /* the first certificate of a PEM file */
{
    const char *b = strstr(pem, "-----BEGIN CERTIFICATE-----"), *e = b ? strstr(b, "-----END CERTIFICATE-----") : 0;
    if (!b || !e) return -1;
    b += 27; size_t o = 0; unsigned v = 0; int bits = 0;
    for (; b < e; b++) {
        int c = *b, x;
        if (c >= 'A' && c <= 'Z') x = c - 'A'; else if (c >= 'a' && c <= 'z') x = c - 'a' + 26;
        else if (c >= '0' && c <= '9') x = c - '0' + 52; else if (c == '+') x = 62; else if (c == '/') x = 63; else continue;
        v = v << 6 | (unsigned)x; bits += 6;
        if (bits >= 8) { bits -= 8; if (o >= cap) return -1; out[o++] = (unsigned char)(v >> bits); }
    }
    return (int)o;
}
static void load_anchors(void)
{
    memcpy(tas, MP_ANCHORS, sizeof MP_ANCHORS); tas_n = MP_ANCHORS_NUM;
    const char *f = getenv("MERAKIPLUS_UPD_TEST_CA"); if (!f) return;
    static char pem[16384]; static unsigned char der[8192];
    FILE *fp = fopen(f, "r"); if (!fp) return;
    size_t n = fread(pem, 1, sizeof pem - 1, fp); fclose(fp); pem[n] = 0;
    int dl = b64_der(pem, der, sizeof der); if (dl <= 0) return;
    br_x509_decoder_context dc; test_dn_len = 0;
    br_x509_decoder_init(&dc, dn_append, NULL);
    br_x509_decoder_push(&dc, der, (size_t)dl);
    br_x509_pkey *pk = br_x509_decoder_get_pkey(&dc);
    if (!pk || !br_x509_decoder_isCA(&dc)) return;
    br_x509_trust_anchor *t = &tas[tas_n];
    t->dn.data = test_dn; t->dn.len = test_dn_len; t->flags = BR_X509_TA_CA; t->pkey = *pk;
    if (pk->key_type == BR_KEYTYPE_RSA) {
        size_t a = pk->key.rsa.nlen, b = pk->key.rsa.elen; if (a + b > sizeof test_key) return;
        memcpy(test_key, pk->key.rsa.n, a); memcpy(test_key + a, pk->key.rsa.e, b);
        t->pkey.key.rsa.n = test_key; t->pkey.key.rsa.e = test_key + a;
    } else {
        if (pk->key.ec.qlen > sizeof test_key) return;
        memcpy(test_key, pk->key.ec.q, pk->key.ec.qlen); t->pkey.key.ec.q = test_key;
    }
    tas_n++;
}
#define ANCHORS tas
#define ANCHORS_N tas_n
#else
#define ANCHORS MP_ANCHORS
#define ANCHORS_N MP_ANCHORS_NUM
static void load_anchors(void) {}
#endif

/* ------------------------------------------------------------ the TLS profile: TLS 1.2, ECDHE + AEAD */
static void tls_init(br_ssl_client_context *cc, br_x509_minimal_context *xc)
{
    static const uint16_t suites[] = {
        BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256, BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
    };
    /* SHA-1 only for certificate signatures some chains may still carry; MD5 is never enabled */
    static const br_hash_class *hashes[] = {&br_sha1_vtable, &br_sha224_vtable, &br_sha256_vtable, &br_sha384_vtable, &br_sha512_vtable};
    br_ssl_client_zero(cc);
    br_ssl_engine_set_versions(&cc->eng, BR_TLS12, BR_TLS12);
    br_x509_minimal_init(xc, &br_sha256_vtable, ANCHORS, ANCHORS_N);
    br_ssl_engine_set_suites(&cc->eng, suites, sizeof suites / sizeof suites[0]);
    br_ssl_engine_set_default_rsavrfy(&cc->eng);
    br_ssl_engine_set_default_ecdsa(&cc->eng);
    br_x509_minimal_set_rsa(xc, br_ssl_engine_get_rsavrfy(&cc->eng));
    br_x509_minimal_set_ecdsa(xc, br_ssl_engine_get_ec(&cc->eng), br_ssl_engine_get_ecdsa(&cc->eng));
    for (size_t i = 0; i < sizeof hashes / sizeof hashes[0]; i++) {
        int id = (int)((hashes[i]->desc >> BR_HASHDESC_ID_OFF) & BR_HASHDESC_ID_MASK);
        br_ssl_engine_set_hash(&cc->eng, id, hashes[i]);
        br_x509_minimal_set_hash(xc, id, hashes[i]);
    }
    br_ssl_engine_set_prf_sha256(&cc->eng, &br_tls12_sha256_prf);
    br_ssl_engine_set_prf_sha384(&cc->eng, &br_tls12_sha384_prf);
    br_ssl_engine_set_default_aes_gcm(&cc->eng);
    br_ssl_engine_set_default_chapol(&cc->eng);
    br_ssl_engine_set_x509(&cc->eng, &xc->vtable);
}

const char *mp_tls_reason(int e)
{
    switch (e) {
    case BR_ERR_X509_NOT_TRUSTED: return "certificate not from a pinned root";
    case BR_ERR_X509_EXPIRED: return "certificate expired or not yet valid";
    case BR_ERR_X509_BAD_SERVER_NAME: return "certificate is for another host";
    case BR_ERR_X509_BAD_SIGNATURE: return "bad certificate signature";
    case BR_ERR_X509_NOT_CA: return "intermediate is not a CA";
    case BR_ERR_X509_EMPTY_CHAIN: return "no certificate";
    case BR_ERR_BAD_VERSION: case BR_ERR_UNSUPPORTED_VERSION: return "server does not speak TLS 1.2";
    case BR_ERR_BAD_CIPHER_SUITE: case BR_ERR_NO_CLIENT_AUTH: return "no common cipher suite";
    case BR_ERR_IO: return "connection lost";
    default: return e >= BR_ERR_SEND_FATAL_ALERT ? "fatal alert" : "handshake failed";
    }
}

/* ------------------------------------------------------------ URLs */
int mp_url_parse(const char *url, char *host, size_t hn, char *path, size_t pn)
{
    if (strncmp(url, "https://", 8)) return -1;                   /* HTTPS only */
    const char *h = url + 8; size_t l = strcspn(h, "/?#");
    if (!l || l >= hn || h[l] != '/') return -1;
    for (size_t i = 0; i < l; i++) {                               /* no port, no user@, lowercase DNS name only */
        char c = h[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) return -1;
    }
    memcpy(host, h, l); host[l] = 0;
    const char *p = h + l;
    if (strlen(p) >= pn || strspn(p, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~/?=&%+:@!$'()*,;") != strlen(p)) return -1;
    snprintf(path, pn, "%s", p);
    return 0;
}
int mp_host_pinned(const char *host)
{
    for (int i = 0; HOSTS[i]; i++) if (!strcmp(host, HOSTS[i])) return 1;
    return 0;
}

/* ------------------------------------------------------------ socket */
static int tcp_connect(mp_http *h, const char *host)
{
    char port[8] = "443", name[128];
    snprintf(name, sizeof name, "%s", host);
#ifdef MERAKIPLUS_UPD_TEST
    const char *ov = getenv("MERAKIPLUS_UPD_TEST_CONNECT");       /* <ip>:<port>: every pinned host is served by the test server */
    if (ov) { const char *c = strrchr(ov, ':'); if (c) { snprintf(name, sizeof name, "%.*s", (int)(c - ov), ov); snprintf(port, sizeof port, "%s", c + 1); } }
#endif
    struct addrinfo hints, *res = 0, *a;
    memset(&hints, 0, sizeof hints); hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    int r = getaddrinfo(name, port, &hints, &res);
    if (r) return nfail(h, NE_DNS, "cannot resolve %s (%s)", host, gai_strerror(r));
    int fd = -1, err = 0;
    for (a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) { err = errno; continue; }
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        int fl = fcntl(fd, F_GETFL); fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) { fcntl(fd, F_SETFL, fl); break; }
        if (errno == EINPROGRESS) {
            long long left = h->deadline - now_ms(); if (left > 10000) left = 10000; if (left < 1) left = 1;
            struct pollfd p = {fd, POLLOUT, 0}; socklen_t sl = sizeof err;
            if (poll(&p, 1, (int)left) == 1 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &sl) == 0 && !err) { fcntl(fd, F_SETFL, fl); break; }
            if (!err) err = ETIMEDOUT;
        } else err = errno;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return nfail(h, NE_CONNECT, "cannot connect to %s (%s)", host, strerror(err));
    struct timeval tv = {IO_TIMEOUT_S, 0};
#ifdef MERAKIPLUS_UPD_TEST
    const char *to = getenv("MERAKIPLUS_UPD_TEST_IO_TIMEOUT");   /* tests: a stalled server times out in seconds, not 20 s */
    if (to && atoi(to) > 0) tv.tv_sec = atoi(to);
#endif
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    h->fd = fd; return 0;
}
static int sock_read(void *ctx, unsigned char *buf, size_t len)
{
    mp_http *h = ctx;
    if (now_ms() > h->deadline) { h->timed_out = 1; return -1; }
    for (;;) {
        ssize_t r = read(h->fd, buf, len);
        if (r > 0) return (int)r;
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) h->timed_out = 1;
        return -1;
    }
}
static int sock_write(void *ctx, const unsigned char *buf, size_t len)
{
    mp_http *h = ctx;
    if (now_ms() > h->deadline) { h->timed_out = 1; return -1; }
    for (;;) {
        ssize_t r = write(h->fd, buf, len);
        if (r > 0) return (int)r;
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) h->timed_out = 1;
        return -1;
    }
}

/* ------------------------------------------------------------ one request (no redirects) */
static br_ssl_client_context cc;
static br_x509_minimal_context xc;
static unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
static br_sslio_context ioc;

static int io_fail(mp_http *h, const char *what)
{
    int e = br_ssl_engine_last_error(&cc.eng);
    if (h->timed_out) return nfail(h, NE_TIMEOUT, "%s: timed out", what);
    if (e != BR_ERR_OK && e != BR_ERR_IO) return nfail(h, NE_TLS, "%s: TLS: %s (%d)", what, mp_tls_reason(e), e);
    return nfail(h, NE_IO, "%s: connection closed", what);
}
static int rd(mp_http *h, unsigned char *b, size_t n)
{
    (void)h;
    return br_sslio_read(&ioc, b, n);
}
static int hdr_value(const char *hdr, const char *name, char *o, size_t n)   /* case-insensitive header lookup */
{
    size_t l = strlen(name);
    for (const char *p = strstr(hdr, "\r\n"); p; p = strstr(p + 2, "\r\n")) {
        const char *q = p + 2;
        if (!strncasecmp(q, name, l) && q[l] == ':') {
            q += l + 1; while (*q == ' ' || *q == '\t') q++;
            size_t m = strcspn(q, "\r\n"); while (m && (q[m - 1] == ' ' || q[m - 1] == '\t')) m--;
            if (m >= n) return -1;
            memcpy(o, q, m); o[m] = 0; return 1;
        }
    }
    return 0;
}

static int one(mp_http *h, const char *host, const char *path, mp_sink sink, void *sctx, char *loc, size_t ln)
{
    int r;
    if ((r = tcp_connect(h, host))) return r;
    int floored; long long t = mp_cert_time((long long)time(NULL), &floored);
    h->clock_floored = floored;
    tls_init(&cc, &xc);
    br_x509_minimal_set_time(&xc, (uint32_t)(t / 86400 + 719528), (uint32_t)(t % 86400));
    br_ssl_engine_set_buffer(&cc.eng, iobuf, sizeof iobuf, 1);
    if (!br_ssl_client_reset(&cc, host, 0)) { close(h->fd); return nfail(h, NE_TLS, "TLS init failed"); }
    br_sslio_init(&ioc, &cc.eng, sock_read, h, sock_write, h);
    static char req[REQ_MAX];
    int rl = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nAccept: */*\r\nAccept-Encoding: identity\r\n"
                                       "Connection: close\r\n\r\n", path, host, h->ua ? h->ua : "merakiplus-upd");
    if (rl <= 0 || rl >= (int)sizeof req) { close(h->fd); return nfail(h, NE_HTTP, "request too long"); }
    if (br_sslio_write_all(&ioc, req, (size_t)rl) || br_sslio_flush(&ioc)) { r = io_fail(h, host); close(h->fd); return r; }
    h->tls_ok = 1;
    /* headers */
    static char hdr[HDR_MAX + 1]; size_t hl = 0; char *end = 0;
    while (!end) {
        if (hl >= HDR_MAX) { close(h->fd); return nfail(h, NE_HTTP, "%s: response header too large", host); }
        int n = rd(h, (unsigned char *)hdr + hl, 1);               /* byte-wise: never read past the header */
        if (n <= 0) { r = io_fail(h, host); close(h->fd); return r; }
        hl += (size_t)n; hdr[hl] = 0;
        if (hl >= 4 && !memcmp(hdr + hl - 4, "\r\n\r\n", 4)) end = hdr + hl;
    }
    int status = 0;
    if (sscanf(hdr, "HTTP/1.%*d %d", &status) != 1 || status < 100 || status > 599) { close(h->fd); return nfail(h, NE_HTTP, "%s: not an HTTP response", host); }
    h->status = status;
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        int ok = hdr_value(hdr, "Location", loc, ln);
        close(h->fd);
        if (ok < 0) return nfail(h, NE_REDIRECT, "%s: redirect URL longer than %d bytes", host, URL_MAX - 1);
        if (ok != 1) return nfail(h, NE_REDIRECT, "%s: redirect without a usable Location", host);
        return -1;                                                  /* caller follows */
    }
    if (status == 404) { close(h->fd); return nfail(h, NE_NOTFOUND, "%s%s: not found (404)", host, path); }
    if (status != 200) { close(h->fd); return nfail(h, NE_HTTP, "%s: HTTP %d", host, status); }
    char v[64]; long long clen = -1; int chunked = 0;
    if (hdr_value(hdr, "Transfer-Encoding", v, sizeof v) == 1) {
        if (strcasecmp(v, "chunked")) { close(h->fd); return nfail(h, NE_HTTP, "%s: unsupported transfer encoding", host); }
        chunked = 1;
    } else if (hdr_value(hdr, "Content-Length", v, sizeof v) == 1) {
        char *e; clen = strtoll(v, &e, 10);
        if (*e || clen < 0) { close(h->fd); return nfail(h, NE_HTTP, "%s: bad Content-Length", host); }
    }
    if (hdr_value(hdr, "Content-Encoding", v, sizeof v) == 1 && strcasecmp(v, "identity")) { close(h->fd); return nfail(h, NE_HTTP, "%s: compressed body", host); }
    if (clen >= 0 && h->limit >= 0 && clen > h->limit) { close(h->fd); return nfail(h, NE_LIMIT, "%s: body of %lld bytes exceeds the limit %lld", host, clen, h->limit); }
    /* body */
    static unsigned char b[8192]; long long got = 0, left = clen;
    for (;;) {
        long long want = sizeof b;
        if (chunked) {
            if (left <= 0) {                                         /* next chunk size line */
                char line[32]; size_t k = 0;
                if (left == 0 && got > 0) { unsigned char crlf[2]; if (rd(h, crlf, 1) <= 0 || rd(h, crlf + 1, 1) <= 0 || crlf[0] != '\r' || crlf[1] != '\n') goto trunc; }
                for (;;) { unsigned char c; if (rd(h, &c, 1) <= 0) goto trunc; if (c == '\n') break; if (k < sizeof line - 1) line[k++] = (char)c; }
                line[k] = 0; char *e; left = strtoll(line, &e, 16);
                if (e == line || left < 0) { close(h->fd); return nfail(h, NE_HTTP, "%s: bad chunk", host); }
                if (left == 0) break;                                /* last chunk (trailers are ignored) */
            }
            if (want > left) want = left;
        } else if (clen >= 0) {
            if (left == 0) break;
            if (want > left) want = left;
        }
        int n = rd(h, b, (size_t)want);
        if (n <= 0) {
            if (clen < 0 && !chunked && br_ssl_engine_last_error(&cc.eng) == BR_ERR_OK && !h->timed_out) break;   /* clean close_notify */
            goto trunc;
        }
        got += n; if (left > 0) left -= n;
        if (h->limit >= 0 && got > h->limit) { close(h->fd); return nfail(h, NE_LIMIT, "%s: body exceeds the limit %lld", host, h->limit); }
        if (sink && sink(sctx, b, (size_t)n)) { close(h->fd); return nfail(h, NE_SINK, "%s", h->sink_err[0] ? h->sink_err : "write failed"); }
        if (h->progress) h->progress(h->pctx, got, clen);
    }
    close(h->fd);
    h->body_len = got;
    return 0;
trunc:
    r = io_fail(h, host);
    close(h->fd);
    if (r == NE_IO) return nfail(h, NE_TRUNC, "%s: body truncated after %lld bytes", host, got);
    return r;
}

int mp_https_get(mp_http *h, const char *url, mp_sink sink, void *sctx)
{
    static int loaded; if (!loaded) { load_anchors(); loaded = 1; }
    static char cur[URL_MAX], path[URL_MAX], loc[URL_MAX]; char host[128];
    if (snprintf(cur, sizeof cur, "%s", url) >= (int)sizeof cur) return nfail(h, NE_URL, "URL longer than %d bytes", URL_MAX - 1);
    h->err[0] = 0; h->kind = 0; h->redirects = 0; h->status = 0; h->body_len = 0; h->timed_out = 0;
    for (;;) {
        if (mp_url_parse(cur, host, sizeof host, path, sizeof path)) return nfail(h, h->redirects ? NE_REDIRECT : NE_URL, "refused URL (HTTPS to a plain host name only): %.80s", cur);
        if (!mp_host_pinned(host)) return nfail(h, h->redirects ? NE_REDIRECT : NE_URL, "refused host %s (not on the pinned list)", host);
        snprintf(h->host, sizeof h->host, "%s", host);
        if (now_ms() > h->deadline) return nfail(h, NE_TIMEOUT, "deadline passed before %s", host);
        int r = one(h, host, path, sink, sctx, loc, sizeof loc);
        if (r >= 0) return r;
        if (++h->redirects > MAX_REDIRECTS) return nfail(h, NE_REDIRECT, "more than %d redirects", MAX_REDIRECTS);
        int cl = loc[0] == '/' && loc[1] != '/' ? snprintf(cur, sizeof cur, "https://%s%s", host, loc)   /* same-host relative */
                                                 : snprintf(cur, sizeof cur, "%s", loc);
        if (cl < 0 || cl >= (int)sizeof cur) return nfail(h, NE_REDIRECT, "redirect URL longer than %d bytes", URL_MAX - 1);
    }
}
