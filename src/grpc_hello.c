/* grpc_hello.c - minimal gRPC unary client built on http_client (HTTP/2 + TLS).
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
 * trailer was not seen. Exit codes: 0 ok, 2 request failed, 3 bad response
 * or non-zero grpc-status, 4 HTTP/2 was not negotiated.
 */
#include "http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define URL      "https://grpcb.in:9001/hello.HelloService/SayHello"
#define GREETING "tech-tab"

int main(void)
{
    unsigned char req[5 + 2 + 64];
    size_t gl = strlen(GREETING), msglen = 2 + gl, i, rl = 0;
    const char *hdrs[] = {
        "Content-Type: application/grpc",
        "TE: trailers",
        "Expect:",
        NULL
    };
    struct http_req rq = {0};
    struct http_resp rs;
    char st[16], reply[128];
    int grpc_status = -1, rc;

    req[0] = 0;
    req[1] = (unsigned char)(msglen >> 24);
    req[2] = (unsigned char)(msglen >> 16);
    req[3] = (unsigned char)(msglen >> 8);
    req[4] = (unsigned char)(msglen);
    req[5] = 0x0A;
    req[6] = (unsigned char)gl;
    memcpy(req + 7, GREETING, gl);

    rq.url = URL;
    rq.post_body = (const char *)req;
    rq.post_len = 5 + msglen;          /* binary body: length must be explicit */
    rq.headers = hdrs;
    rq.timeout_s = 10;
    rq.http2 = 1;

    rc = http_request(&rq, &rs);
    if (rc == -3) {
        fprintf(stderr, "HTTP/2 was not negotiated\n");
        http_free(&rs);
        return 4;
    }
    if (rc != 0 || rs.status != 200) {
        fprintf(stderr, "request failed (code %d, http %ld)\n", rc, rs.status);
        http_free(&rs);
        return 2;
    }
    if (http_header(&rs, "grpc-status", st, sizeof st))
        grpc_status = atoi(st);
    if (grpc_status > 0) {
        fprintf(stderr, "grpc status %d\n", grpc_status);
        http_free(&rs);
        return 3;
    }
    if (rs.len < 7 || rs.data[0] != 0) {
        fprintf(stderr, "unexpected gRPC response framing\n");
        http_free(&rs);
        return 3;
    }
    {
        size_t ml = ((size_t)rs.data[1] << 24) | ((size_t)rs.data[2] << 16) |
                    ((size_t)rs.data[3] << 8) | (size_t)rs.data[4];
        if (5 + ml > rs.len || ml < 2 || rs.data[5] != 0x0A || rs.data[6] >= 128 ||
            (size_t)rs.data[6] > ml - 2) {
            fprintf(stderr, "cannot parse HelloResponse\n");
            http_free(&rs);
            return 3;
        }
        for (i = 0; i < rs.data[6] && rl < sizeof reply - 1; i++) {
            unsigned char ch = rs.data[7 + i];
            reply[rl++] = (ch >= 32 && ch < 127) ? (char)ch : '?';
        }
        reply[rl] = '\0';
    }
    printf("%d %ld %s\n", grpc_status, (long)rs.total_ms, reply);
    http_free(&rs);
    return 0;
}

