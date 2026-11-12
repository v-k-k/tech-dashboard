/* gql_trains.c - POSTs a GraphQL query to Digitraffic (Finnish railways)
 * and counts the trains currently running. Prints "<trains> <milliseconds>".
 * Exit codes: 0 ok, 2 download error, 3 GraphQL error in the response.
 */
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define URL   "https://rata.digitraffic.fi/api/v2/graphql/graphql"
#define QUERY "{\"query\":\"{ currentlyRunningTrains { trainNumber } }\"}"
#define MAXBYTES (4u * 1024u * 1024u)

struct buf { char *d; size_t n, cap; };

static size_t write_cb(void *p, size_t sz, size_t nm, void *u)
{
    struct buf *b = u;
    size_t k = sz * nm;
    if (b->n + k + 1 > MAXBYTES) return 0;
    if (b->n + k + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 65536;
        while (nc < b->n + k + 1) nc *= 2;
        char *t = realloc(b->d, nc);
        if (!t) return 0;
        b->d = t;
        b->cap = nc;
    }
    memcpy(b->d + b->n, p, k);
    b->n += k;
    b->d[b->n] = '\0';
    return k;
}

int main(void)
{
    struct buf b = {0, 0, 0};
    struct curl_slist *h = NULL;
    CURL *c;
    CURLcode rc;
    long http = 0, count = 0;
    double t = 0;
    const char *p;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    c = curl_easy_init();
    if (!c) return 2;
    h = curl_slist_append(h, "Content-Type: application/json");
    h = curl_slist_append(h, "Accept: application/json");
    h = curl_slist_append(h, "Digitraffic-User: tech-tab-demo");
    curl_easy_setopt(c, CURLOPT_URL, URL);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, QUERY);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "tech-tab/1.0");
    rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_getinfo(c, CURLINFO_TOTAL_TIME, &t);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    curl_global_cleanup();

    if (rc != CURLE_OK || http != 200 || !b.d) {
        fprintf(stderr, "request failed (curl %d, http %ld)\n", (int)rc, http);
        free(b.d);
        return 2;
    }
    if (strstr(b.d, "\"errors\"")) {
        fprintf(stderr, "GraphQL returned errors\n");
        free(b.d);
        return 3;
    }
    for (p = b.d; (p = strstr(p, "\"trainNumber\"")) != NULL; p += 13)
        count++;
    printf("%ld %ld\n", count, (long)(t * 1000.0));
    free(b.d);
    return 0;
}

