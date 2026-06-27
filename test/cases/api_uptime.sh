#!/usr/bin/env bash
# A site carries a created_at in the public listing, and the panel exposes a
# seven-day uptime history that mirrors the recorded liveness buckets. The schema
# is created by a short pre-run, then the rows are seeded while the server is
# down, so the listing is deterministic.
set -u
PORT=18772
DB=$(mktemp -u /tmp/wr_uptime_XXXXXX.db)
AJAR=$(mktemp -u /tmp/wr_uptime_jar_XXXXXX)

timeout 2 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1
now_hour=$(( $(date +%s) / 3600 ))
sqlite3 "$DB" "INSERT INTO sites (slug,name,url,description,is_reachable,last_seen_at,owner,created_at) VALUES ('up','Up','https://up.example','',1,9999999999,'dev:admin',7);"
sqlite3 "$DB" "INSERT INTO liveness_buckets (slug,hour_bucket,up_count,probe_count) VALUES ('up',$now_hour,3,4),('up',$((now_hour-1)),0,2),('up',$((now_hour-5)),5,5);"

timeout 15 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

curl -s -c "$AJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=admin"
echo "sites: $(curl -s "http://127.0.0.1:$PORT/sites" | sed 's/"created_at":[0-9]*/"created_at":0/g')"

me=$(curl -s -b "$AJAR" "http://127.0.0.1:$PORT/api/v1/me")
uptime=$(printf '%s' "$me" | sed 's/.*"uptime":\[//; s/\].*//')
echo "uptime-length: $(printf '%s' "$uptime" | tr ',' '\n' | grep -c .)"
echo "uptime-samples: $(printf '%s' "$uptime" | tr ',' '\n' | grep -vx -- -1 | tr '\n' ' ')"

kill "$server" 2>/dev/null
rm -rf "$DB" "$AJAR"
