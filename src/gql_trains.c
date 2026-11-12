/* gql_trains.c - POSTs a GraphQL query to Digitraffic (Finnish railways)
 * and counts the trains currently running. Prints "<trains> <milliseconds>".
 * Exit codes: 0 ok, 2 request failed, 3 GraphQL error in the response.
 */
#include "http_client.h"
#include <stdio.h>
#include <string.h>

#define URL   "https://rata.digitraffic.fi/api/v2/graphql/graphql"
#define QUERY "{\"query\":\"{ currentlyRunningTrains { trainNumber } }\"}"

int main(void)
{
    const char *hdrs[] = {
        "Content-Type: application/json",
        "Accept: application/json",
        "Digitraffic-User: tech-tab-demo",
        NULL
    };
    struct http_req rq = {0};
    struct http_resp rs;
    long count = 0;
    const char *p;
    int rc;

    rq.url = URL;
    rq.post_body = QUERY;
    rq.headers = hdrs;
    rq.timeout_s = 10;
    rq.compress = 1;

    rc = http_request(&rq, &rs);
    if (rc != 0 || rs.status != 200 || !rs.data) {
        fprintf(stderr, "request failed (code %d, http %ld)\n", rc, rs.status);
        http_free(&rs);
        return 2;
    }
    if (strstr((char *)rs.data, "\"errors\"")) {
        fprintf(stderr, "GraphQL returned errors\n");
        http_free(&rs);
        return 3;
    }
    for (p = (char *)rs.data; (p = strstr(p, "\"trainNumber\"")) != NULL; p += 13)
        count++;
    printf("%ld %ld\n", count, (long)rs.total_ms);
    http_free(&rs);
    return 0;
}

