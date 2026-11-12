/* grpc_hello.c - minimal gRPC unary client built on libcurl (HTTP/2 + TLS).
 * Calls hello.HelloService/SayHello on grpcb.in:9001.
 *
 * gRPC wire format of a unary call:
 *   - HTTP/2 POST, content-type: application/grpc, te: trailers
 *   - body = 1 byte compression flag (0) + 4 byte big-endian length + protobuf message
 *   - HelloRequest { string greeting = 1; }  -> 0x0A <len> <bytes>
 *   - HelloResponse { string reply = 1; }    -> same layout
 *   - the call result is reported in the grpc-status trailer (0 = OK)
 *
 * Prints "<grpc_status> <milliseconds> <reply>". grpc_status is -1 if the
 * trailer was not seen. Exit codes: 0 ok, 2 request failed, 3 bad response,
 * 4 HTTP/2 was not negotiated.
 */
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define URL      "https://grpcb.in:9001/hello.HelloService/SayHello"
#define GREETING "tech-tab"
#define MAXBODY  65536

static unsigned char body[MAXBODY];
static size_t blen = 0;
static int grpc_status = -1;

static size_t write_cb(void *p, size_t sz, size_t nm, void *u)
{
    size_t k = sz * nm;
    (void)u;
    if (blen + k > sizeof body) return 0;
    memcpy(body + blen, p, k);
    blen += k;
    return k;
}

static size_t header_cb(char *line, size_t sz, size_t nm, void *u)
{
    size_t k = sz * nm;
    (void)u;
    if (k > 12 && strncasecmp(line, "grpc-status:", 12) == 0)
        grpc_status = atoi(line + 12);
    return k;
}

int main(void)
{
    unsigned char req[5 + 2 + 64];
    size_t gl = strlen(GREETING), msglen = 2 + gl, i;
    struct curl_slist *h = NULL;
    CURL *c;
    CURLcode rc;
    long http = 0, hv = 0;
    double t = 0;
    char reply[128];
    size_t rl = 0;

    req[0] = 0;
    req[1] = (unsigned char)(msglen >> 24);
    req[2] = (unsigned char)(msglen >> 16);
    req[3] = (unsigned char)(msglen >> 8);
    req[4] = (unsigned char)(msglen);
    req[5] = 0x0A;
    req[6] = (unsigned char)gl;
    memcpy(req + 7, GREETING, gl);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    c = curl_easy_init();
    if (!c) return 2;
    h = curl_slist_append(h, "Content-Type: application/grpc");
    h = curl_slist_append(h, "TE: trailers");
    h = curl_slist_append(h, "Expect:");
    curl_easy_setopt(c, CURLOPT_URL, URL);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, (char *)req);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)(5 + msglen));
    curl_easy_setopt(c, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "tech-tab/1.0");
    rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_getinfo(c, CURLINFO_HTTP_VERSION, &hv);
    curl_easy_getinfo(c, CURLINFO_TOTAL_TIME, &t);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    curl_global_cleanup();

    if (rc != CURLE_OK || http != 200) {
        fprintf(stderr, "request failed (curl %d, http %ld)\n", (int)rc, http);
        return 2;
    }
    if (hv != CURL_HTTP_VERSION_2_0) {
        fprintf(stderr, "HTTP/2 was not negotiated\n");
        return 4;
    }
    if (blen < 7 || body[0] != 0) {
        fprintf(stderr, "unexpected gRPC response framing\n");
        return 3;
    }
    {
        size_t ml = ((size_t)body[1] << 24) | ((size_t)body[2] << 16) |
                    ((size_t)body[3] << 8) | (size_t)body[4];
        if (5 + ml > blen || ml < 2 || body[5] != 0x0A || body[6] >= 128 ||
            (size_t)body[6] > ml - 2) {
            fprintf(stderr, "cannot parse HelloResponse\n");
            return 3;
        }
        for (i = 0; i < body[6] && rl < sizeof reply - 1; i++) {
            unsigned char ch = body[7 + i];
            reply[rl++] = (ch >= 32 && ch < 127) ? (char)ch : '?';
        }
        reply[rl] = '\0';
    }
    printf("%d %ld %s\n", grpc_status, (long)(t * 1000.0), reply);
    return 0;
}

