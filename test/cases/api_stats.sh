#!/usr/bin/env bash
# The traffic stats record a click and a hop only when the server runs with
# --enable-metrics. A seeded ring is clicked and hopped, the admin reads the
# tallies back, and a second server without the flag records nothing.
set -u
PORT=18776
DBPORT=18777
DB=$(mktemp -u /tmp/wr_stats_XXXXXX.db)
DBOFF=$(mktemp -u /tmp/wr_stats_off_XXXXXX.db)
AJAR=$(mktemp -u /tmp/wr_stats_jar_XXXXXX)

timeout 2 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1
sqlite3 "$DB" "INSERT INTO sites (slug,name,url,description,is_reachable,last_seen_at,owner_source,owner_name,created_at) VALUES ('a','Site A','https://a.example','',1,9999999999,0,'x',1),('b','Site B','https://b.example','',1,9999999999,0,'x',2);"

timeout 15 "$BIN" --enable-dangerous-developer-environment --enable-metrics --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

curl -s -c "$AJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=admin"
api="http://127.0.0.1:$PORT/api/v1"
ct='Content-Type: application/json'

curl -s -o /dev/null -X POST -H "$ct" -d '{"slug":"a"}' "$api/sites/click"
curl -s -o /dev/null -X POST -H "$ct" -d '{"slug":"a"}' "$api/sites/click"
curl -s -o /dev/null -X POST -H "$ct" -d '{"slug":"a"}' "$api/sites/click"
curl -s -o /dev/null "http://127.0.0.1:$PORT/a/next"

echo "stats: $(curl -s -b "$AJAR" "$api/admin/stats")"
echo "click-missing: $(curl -s -o /dev/null -w '%{http_code}' -X POST -H "$ct" -d '{}' "$api/sites/click")"

kill "$server" 2>/dev/null

timeout 15 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$DBPORT" -d "$DBOFF" -u http://x >/dev/null 2>&1 &
server_off=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$DBPORT/api/v1/config"

sqlite3 "$DBOFF" "INSERT INTO sites (slug,name,url,description,is_reachable,last_seen_at,owner_source,owner_name,created_at) VALUES ('a','Site A','https://a.example','',1,9999999999,0,'x',1);"
curl -s -c "$AJAR" -o /dev/null "http://127.0.0.1:$DBPORT/auth/dev?role=admin"
echo "click-disabled: $(curl -s -X POST -H "$ct" -d '{"slug":"a"}' "http://127.0.0.1:$DBPORT/api/v1/sites/click")"
echo "stats-disabled: $(curl -s -b "$AJAR" "http://127.0.0.1:$DBPORT/api/v1/admin/stats")"

kill "$server_off" 2>/dev/null
rm -rf "$DB" "$DBOFF" "$AJAR"
