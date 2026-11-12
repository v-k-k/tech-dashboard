#!/bin/bash
# Returns an HTML fragment (table) for the "Tech general" tab.

esc() { sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'; }
row() { printf '<tr><td>%s</td><td>%s</td></tr>\n' "$1" "$(printf '%s' "$2" | esc)"; }

echo "Content-Type: text/html; charset=utf-8"
echo "Cache-Control: no-store"
echo

echo '<table>'
echo '<thead><tr><th>Parameter</th><th>Value</th></tr></thead>'
echo '<tbody>'
row "Server time" "$(date '+%F %T %Z')"
row "Hostname" "$(hostname)"
row "Kernel" "$(uname -sr)"
row "Uptime" "$(uptime -p)"
row "Load average (1/5/15 min)" "$(cut -d' ' -f1-3 /proc/loadavg)"
row "Memory used / total" "$(free -m | awk '/^Mem:/ {print $3" MB / "$2" MB"}')"
row "Disk / used" "$(df -h / | awk 'NR==2 {print $3" of "$2" ("$5")"}')"
echo '</tbody>'
echo '</table>'

HFP_CACHE=/tmp/hfp.cache
if [ -z "$(find "$HFP_CACHE" -mmin -1 2>/dev/null)" ]; then
  timeout 15 /usr/local/bin/hfp_count 8 tram > "$HFP_CACHE.tmp" 2>/dev/null && mv "$HFP_CACHE.tmp" "$HFP_CACHE"
fi
read -r hv hm < "$HFP_CACHE" 2>/dev/null
[[ "$hv" =~ ^[0-9]+$ ]] || hv="n/a"
[[ "$hm" =~ ^[0-9]+$ ]] || hm="0"
echo '<h3>Helsinki trams in motion (HSL MQTT)</h3>'
printf '<p><strong style="font-size:1.6rem">%s</strong> trams seen in an 8-second sample (%s messages). Source: HSL open HFP over MQTT.</p>\n' "$hv" "$hm"

KRK_URL="https://gtfs.ztp.krakow.pl/VehiclePositions_T.pb"
KRK_CACHE=/tmp/krk.cache
if [ -z "$(find "$KRK_CACHE" -mmin -1 2>/dev/null)" ]; then
  timeout 15 /usr/local/bin/gtfsrt_count "$KRK_URL" > "$KRK_CACHE.tmp" 2>/dev/null && mv "$KRK_CACHE.tmp" "$KRK_CACHE"
fi
read -r kv ka < "$KRK_CACHE" 2>/dev/null
[[ "$kv" =~ ^[0-9]+$ ]] || kv="n/a"
[[ "$ka" =~ ^-?[0-9]+$ ]] || ka="?"
echo '<h3>Kraków trams in the feed (ZTP GTFS-RT)</h3>'
printf '<p><strong style="font-size:1.6rem">%s</strong> tram vehicles reporting position (feed age: %s s). Source: ZTP Kraków open data, GTFS Realtime over HTTP.</p>\n' "$kv" "$ka"

GQL_CACHE=/tmp/gql.cache
if [ -z "$(find "$GQL_CACHE" -mmin -1 2>/dev/null)" ]; then
  timeout 15 /usr/local/bin/gql_trains > "$GQL_CACHE.tmp" 2>/dev/null && mv "$GQL_CACHE.tmp" "$GQL_CACHE"
fi
read -r gq_n gq_ms < "$GQL_CACHE" 2>/dev/null
[[ "$gq_n" =~ ^[0-9]+$ ]] || gq_n="n/a"
[[ "$gq_ms" =~ ^[0-9]+$ ]] || gq_ms="?"
echo '<h3>GraphQL: trains running in Finland (Digitraffic)</h3>'
printf '<p><strong style="font-size:1.6rem">%s</strong> trains running now (query took %s ms). Source: Digitraffic rail GraphQL API, client written in C with libcurl.</p>\n' "$gq_n" "$gq_ms"

GRPC_CACHE=/tmp/grpc.cache
if [ -z "$(find "$GRPC_CACHE" -mmin -1 2>/dev/null)" ]; then
  timeout 15 /usr/local/bin/grpc_hello > "$GRPC_CACHE.tmp" 2>/dev/null && mv "$GRPC_CACHE.tmp" "$GRPC_CACHE"
fi
read -r gr_st gr_ms gr_reply < "$GRPC_CACHE" 2>/dev/null
[[ "$gr_st" =~ ^[0-9]+$ ]] || gr_st="?"
[[ "$gr_ms" =~ ^[0-9]+$ ]] || gr_ms="?"
gr_reply=$(printf '%s' "${gr_reply:-n/a}" | esc)
echo '<h3>gRPC: public test server (grpcb.in)</h3>'
printf '<p>Reply: <strong>%s</strong> (gRPC status %s, %s ms). Unary call hello.HelloService/SayHello over HTTP/2 + TLS, client written in C with libcurl.</p>\n' "$gr_reply" "$gr_st" "$gr_ms"
