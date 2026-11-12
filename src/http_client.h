#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H
#include <stddef.h>

struct http_req {
    const char  *url;
    const char  *prefix;      /* if not NULL, url must start with it (safety check) */
    const char  *post_body;   /* NULL for GET */
    size_t       post_len;    /* body length; 0 means strlen(post_body); set it for binary bodies */
    const char **headers;     /* NULL-terminated array of "Name: value", may be NULL */
    long         timeout_s;   /* 0 means 10 */
    size_t       max_bytes;   /* 0 means 8 MB */
    int          http2;       /* 1 = require HTTP/2 over TLS */
    int          compress;    /* 1 = send Accept-Encoding (gzip etc.) */
};

struct http_resp {
    unsigned char *data;      /* malloc'ed, NUL-terminated; free with http_free() */
    size_t         len;
    long           status;    /* HTTP status code */
    double         total_ms;
    int            http_ver;  /* 2 if HTTP/2 was negotiated */
    char           headers[2048]; /* raw response headers and trailers, truncated */
};

/* 0 = transport ok (check resp->status yourself), -1 = bad arguments,
 * -2 = transport error, -3 = HTTP/2 required but not negotiated */
int  http_request(const struct http_req *rq, struct http_resp *rs);

/* copies the value of a response header into out; returns out or NULL */
const char *http_header(const struct http_resp *rs, const char *name, char *out, size_t outlen);

void http_free(struct http_resp *rs);
#endif

