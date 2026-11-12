#include "http_client.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DEF_TIMEOUT  10L
#define DEF_MAXBYTES (8u * 1024u * 1024u)

struct sink  { unsigned char *d; size_t n, cap, max; };
struct hsink { char *buf; size_t cap, n; };

static size_t body_cb(void *p, size_t sz, size_t nm, void *u)
{
    struct sink *s = u;
    size_t k = sz * nm;
    if (s->n + k + 1 > s->max) return 0;
    if (s->n + k + 1 > s->cap) {
        size_t nc = s->cap ? s->cap : 65536;
        unsigned char *t;
        while (nc < s->n + k + 1) nc *= 2;
        t = realloc(s->d, nc);
        if (!t) return 0;
        s->d = t;
        s->cap = nc;
    }
    memcpy(s->d + s->n, p, k);
    s->n += k;
    s->d[s->n] = '\0';
    return k;
}

static size_t hdr_cb(char *line, size_t sz, size_t nm, void *u)
{
    struct hsink *h = u;
    size_t k = sz * nm;
    if (h->n + k + 1 < h->cap) {
        memcpy(h->buf + h->n, line, k);
        h->n += k;
        h->buf[h->n] = '\0';
    }
    return k;
}

int http_request(const struct http_req *rq, struct http_resp *rs)
{
    static int inited = 0;
    struct sink s = {0, 0, 0, 0};
    struct hsink h;
    struct curl_slist *hl = NULL;
    CURL *c;
    CURLcode rc;
    long hv = 0;
    double t = 0;
    const char **p;

    if (!rs) return -1;
    memset(rs, 0, sizeof *rs);
    if (!rq || !rq->url) return -1;
    if (rq->prefix && strncmp(rq->url, rq->prefix, strlen(rq->prefix)) != 0)
        return -1;

    if (!inited) { curl_global_init(CURL_GLOBAL_DEFAULT); inited = 1; }
    s.max = rq->max_bytes ? rq->max_bytes : DEF_MAXBYTES;
    h.buf = rs->headers;
    h.cap = sizeof rs->headers;
    h.n = 0;

    c = curl_easy_init();
    if (!c) return -2;
    for (p = rq->headers; p && *p; p++) hl = curl_slist_append(hl, *p);

    curl_easy_setopt(c, CURLOPT_URL, rq->url);
    if (hl) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    if (rq->post_body) {
        size_t len = rq->post_len ? rq->post_len : strlen(rq->post_body);
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, rq->post_body);
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)len);
    }
    if (rq->http2)
        curl_easy_setopt(c, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
    if (rq->compress)
        curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, body_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, hdr_cb);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &h);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, rq->timeout_s ? rq->timeout_s : DEF_TIMEOUT);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "tech-tab/1.0");

    rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &rs->status);
    curl_easy_getinfo(c, CURLINFO_TOTAL_TIME, &t);
    curl_easy_getinfo(c, CURLINFO_HTTP_VERSION, &hv);
    curl_slist_free_all(hl);
    curl_easy_cleanup(c);

    rs->total_ms = t * 1000.0;
    rs->http_ver = (hv == CURL_HTTP_VERSION_2_0) ? 2 : 0;
    if (rc != CURLE_OK) { free(s.d); return -2; }
    rs->data = s.d;
    rs->len = s.n;
    if (rq->http2 && rs->http_ver != 2) return -3;
    return 0;
}

const char *http_header(const struct http_resp *rs, const char *name, char *out, size_t outlen)
{
    size_t nl = strlen(name);
    const char *p = rs->headers;
    if (outlen == 0) return NULL;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t ll = e ? (size_t)(e - p) : strlen(p);
        if (ll > nl + 1 && strncasecmp(p, name, nl) == 0 && p[nl] == ':') {
            const char *v = p + nl + 1;
            size_t vl;
            while (v < p + ll && (*v == ' ' || *v == '\t')) v++;
            vl = (size_t)(p + ll - v);
            while (vl && (v[vl - 1] == '\r' || v[vl - 1] == ' ')) vl--;
            if (vl >= outlen) vl = outlen - 1;
            memcpy(out, v, vl);
            out[vl] = '\0';
            return out;
        }
        if (!e) break;
        p = e + 1;
    }
    return NULL;
}

void http_free(struct http_resp *rs)
{
    free(rs->data);
    rs->data = NULL;
    rs->len = 0;
}

