/* merakiplus_upd_net.h — the updater's HTTPS client (src/merakiplus_upd_net.c). */
#ifndef MERAKIPLUS_UPD_NET_H
#define MERAKIPLUS_UPD_NET_H
#include <stddef.h>

/* error kinds (mp_http.kind) */
enum { NE_OK = 0, NE_URL = 1, NE_DNS, NE_CONNECT, NE_TLS, NE_TIMEOUT, NE_IO, NE_HTTP, NE_NOTFOUND, NE_REDIRECT, NE_LIMIT,
       NE_TRUNC, NE_SINK };

typedef int (*mp_sink)(void *ctx, const unsigned char *p, size_t n);          /* != 0: abort (set mp_http.sink_err) */
typedef void (*mp_progress)(void *ctx, long long got, long long total);

typedef struct {
    /* in */
    const char *ua;                 /* User-Agent (no identifiers) */
    long long deadline;             /* CLOCK_MONOTONIC ms: the whole request incl. redirects */
    long long limit;                /* max body bytes (-1 = none) */
    mp_progress progress; void *pctx;
    char sink_err[120];
    /* out */
    int kind, status, redirects, tls_ok, clock_floored, timed_out, fd;
    long long body_len;
    char host[128], err[200];
} mp_http;

int mp_https_get(mp_http *h, const char *url, mp_sink sink, void *sctx);      /* 0 or an NE_* kind (h->err) */
int mp_url_parse(const char *url, char *host, size_t hn, char *path, size_t pn);
int mp_host_pinned(const char *host);
long long mp_cert_time(long long now, int *floored);
const char *mp_tls_reason(int e);
#endif
