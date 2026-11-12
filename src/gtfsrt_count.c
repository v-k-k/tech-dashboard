/* gtfsrt_count.c - counts vehicle entities in a GTFS-Realtime VehiclePositions feed.
 * Usage: gtfsrt_count <url>        (url must start with https://gtfs.ztp.krakow.pl/)
 * Prints "<vehicles> <feed_age_seconds>"  (age is -1 if the header has no timestamp)
 * Exit codes: 0 ok, 1 bad arguments, 2 download error, 3 parse error.
 *
 * Minimal protobuf wire-format reader, no schema needed. Field numbers come
 * from the GTFS-Realtime specification:
 *   FeedMessage: header = 1, entity = 2 (repeated)
 *   FeedHeader:  timestamp = 3 (varint, POSIX seconds)
 *   FeedEntity:  vehicle = 4
 * An entity that contains field 4 is one vehicle position.
 */
#include "http_client.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PREFIX "https://gtfs.ztp.krakow.pl/"

static int rd_varint(const unsigned char **p, const unsigned char *end, uint64_t *v)
{
    uint64_t r = 0;
    int shift = 0;
    while (*p < end && shift < 64) {
        unsigned char c = *(*p)++;
        r |= (uint64_t)(c & 0x7f) << shift;
        if (!(c & 0x80)) { *v = r; return 1; }
        shift += 7;
    }
    return 0;
}

static int skip(const unsigned char **p, const unsigned char *end, unsigned wt)
{
    uint64_t v;
    switch (wt) {
    case 0: return rd_varint(p, end, &v);
    case 1: if (end - *p < 8) return 0; *p += 8; return 1;
    case 2: if (!rd_varint(p, end, &v) || v > (uint64_t)(end - *p)) return 0;
            *p += v; return 1;
    case 5: if (end - *p < 4) return 0; *p += 4; return 1;
    default: return 0;
    }
}

static int parse(const unsigned char *d, size_t n, long *vehicles, uint64_t *ts)
{
    const unsigned char *p = d, *end = d + n;
    *vehicles = 0;
    *ts = 0;
    while (p < end) {
        uint64_t tag, len;
        unsigned fn, wt;
        if (!rd_varint(&p, end, &tag)) return 0;
        fn = (unsigned)(tag >> 3);
        wt = (unsigned)(tag & 7);

        if (wt == 2 && (fn == 1 || fn == 2)) {
            const unsigned char *sub, *sube;
            if (!rd_varint(&p, end, &len) || len > (uint64_t)(end - p)) return 0;
            sub = p;
            sube = p + len;
            p = sube;

            if (fn == 1) {                       /* FeedHeader */
                while (sub < sube) {
                    uint64_t t2;
                    if (!rd_varint(&sub, sube, &t2)) return 0;
                    if ((t2 >> 3) == 3 && (t2 & 7) == 0) {
                        if (!rd_varint(&sub, sube, ts)) return 0;
                    } else if (!skip(&sub, sube, (unsigned)(t2 & 7))) return 0;
                }
            } else {                             /* FeedEntity */
                int has_vehicle = 0;
                while (sub < sube) {
                    uint64_t t3;
                    if (!rd_varint(&sub, sube, &t3)) return 0;
                    if ((t3 >> 3) == 4 && (t3 & 7) == 2) has_vehicle = 1;
                    if (!skip(&sub, sube, (unsigned)(t3 & 7))) return 0;
                }
                if (has_vehicle) (*vehicles)++;
            }
        } else if (!skip(&p, end, wt)) {
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    struct http_req rq = {0};
    struct http_resp rs;
    long vehicles = 0;
    uint64_t ts = 0;
    int rc;

    if (argc != 2 || strncmp(argv[1], PREFIX, strlen(PREFIX)) != 0) {
        fprintf(stderr, "usage: %s %s<file>.pb\n", argv[0], PREFIX);
        return 1;
    }
    rq.url = argv[1];
    rq.prefix = PREFIX;
    rq.timeout_s = 10;
    rq.compress = 1;

    rc = http_request(&rq, &rs);
    if (rc != 0 || rs.status != 200 || rs.len == 0) {
        fprintf(stderr, "download failed (code %d, http %ld)\n", rc, rs.status);
        http_free(&rs);
        return 2;
    }
    if (!parse(rs.data, rs.len, &vehicles, &ts)) {
        fprintf(stderr, "not a valid protobuf feed\n");
        http_free(&rs);
        return 3;
    }
    printf("%ld %lld\n", vehicles, ts ? (long long)time(NULL) - (long long)ts : -1LL);
    http_free(&rs);
    return 0;
}

