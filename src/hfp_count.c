/* hfp_count.c - counts distinct vehicles seen on HSL HFP (MQTT over TCP).
 * Usage: hfp_count [seconds] [mode]     mode: tram|bus|metro|train|ferry
 * Prints "<distinct_vehicles> <messages>" on success.
 * Exit codes: 0 ok, 1 bad arguments, 2 connection error.
 *
 * Topic layout (HFP v2): /hfp/v2/journey/ongoing/vp/<mode>/<operator>/<vehicle>/...
 * so a vehicle is identified by the pair <operator>/<vehicle> from the topic.
 */
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HOST    "mqtt.hsl.fi"
#define PORT    1883
#define MAXV    2048

static char topic_filter[96];
static char seen[MAXV][24];
static int  nseen = 0;
static long nmsg  = 0;

static void on_connect(struct mosquitto *m, void *obj, int rc)
{
    (void)obj;
    if (rc == 0)
        mosquitto_subscribe(m, NULL, topic_filter, 0);
}

static void on_message(struct mosquitto *m, void *obj, const struct mosquitto_message *msg)
{
    char buf[256], key[24], *tok[12], *save = NULL, *p;
    int n = 0, i;
    (void)m; (void)obj;

    nmsg++;
    if (!msg->topic || strlen(msg->topic) >= sizeof buf) return;
    strcpy(buf, msg->topic);

    for (p = strtok_r(buf, "/", &save); p && n < 12; p = strtok_r(NULL, "/", &save))
        tok[n++] = p;
    /* tok: 0 hfp,1 v2,2 journey,3 ongoing,4 vp,5 mode,6 operator,7 vehicle */
    if (n < 8) return;

    snprintf(key, sizeof key, "%s/%s", tok[6], tok[7]);
    for (i = 0; i < nseen; i++)
        if (strcmp(seen[i], key) == 0) return;
    if (nseen < MAXV)
        strcpy(seen[nseen++], key);
}

static int valid_mode(const char *s)
{
    static const char *ok[] = {"tram", "bus", "metro", "train", "ferry"};
    for (size_t i = 0; i < sizeof ok / sizeof *ok; i++)
        if (strcmp(s, ok[i]) == 0) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 8;
    const char *mode = argc > 2 ? argv[2] : "tram";
    struct mosquitto *m;
    time_t end;

    if (secs < 1 || secs > 30 || !valid_mode(mode)) {
        fprintf(stderr, "usage: %s [1-30 seconds] [tram|bus|metro|train|ferry]\n", argv[0]);
        return 1;
    }
    snprintf(topic_filter, sizeof topic_filter, "/hfp/v2/journey/ongoing/vp/%s/#", mode);

    mosquitto_lib_init();
    m = mosquitto_new(NULL, true, NULL);
    if (!m) { mosquitto_lib_cleanup(); return 2; }
    mosquitto_connect_callback_set(m, on_connect);
    mosquitto_message_callback_set(m, on_message);

    if (mosquitto_connect(m, HOST, PORT, 20) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "connect failed\n");
        mosquitto_destroy(m);
        mosquitto_lib_cleanup();
        return 2;
    }

    end = time(NULL) + secs;
    while (time(NULL) < end) {
        if (mosquitto_loop(m, 500, 1) != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "connection lost\n");
            mosquitto_destroy(m);
            mosquitto_lib_cleanup();
            return 2;
        }
    }

    printf("%d %ld\n", nseen, nmsg);
    mosquitto_disconnect(m);
    mosquitto_destroy(m);
    mosquitto_lib_cleanup();
    return 0;
}

