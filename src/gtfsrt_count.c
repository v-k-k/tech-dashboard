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
#include <curl/curl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXBYTES (8u * 1024u * 1024u)
#define PREFIX   "https://gtfs.ztp.krakow.pl/"

struct buf { unsigned char *d; size_t n, cap; };

static size_t write_cb(void *p, size_t sz, size_t nm, void *u)
{
    struct buf *b = u;
    size_t k = sz * nm;
    if (b->n + k > MAXBYTES) return 0;
    if (b->n + k > b->cap) {
        size_t nc = b->cap ? b->cap : 65536;
        while (nc < b->n + k) nc *= 2;
        unsigned char *t = realloc(b->d, nc);
        if (!t) return 0;
        b->d = t;
        b->cap = nc;
    }
    memcpy(b->d + b->n, p, k);
    b->n += k;
    return k;
}

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
    struct buf b = {0, 0, 0};
    CURL *c;
    CURLcode rc;
    long http = 0, vehicles = 0;
    uint64_t ts = 0;

    if (argc != 2 || strncmp(argv[1], PREFIX, strlen(PREFIX)) != 0) {
        fprintf(stderr, "usage: %s %s<file>.pb\n", argv[0], PREFIX);
        return 1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    c = curl_easy_init();
    if (!c) return 2;
    curl_easy_setopt(c, CURLOPT_URL, argv[1]);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_USERAGENT, "tech-tab/1.0");
    rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(c);
    curl_global_cleanup();

    if (rc != CURLE_OK || http != 200 || b.n == 0) {
        fprintf(stderr, "download failed (curl %d, http %ld)\n", (int)rc, http);
        free(b.d);
        return 2;
    }
    if (!parse(b.d, b.n, &vehicles, &ts)) {
        fprintf(stderr, "not a valid protobuf feed\n");
        free(b.d);
        return 3;
    }
    printf("%ld %lld\n", vehicles, ts ? (long long)time(NULL) - (long long)ts : -1LL);
    free(b.d);
    return 0;
}

