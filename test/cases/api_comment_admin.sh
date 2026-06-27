#!/usr/bin/env bash
# A comment posted by an admin who owns a site in the ring is approved at once,
# so it appears in the public listing without a separate approval and the
# pending queue stays empty.
set -u
PORT=18775
DB=$(mktemp -u /tmp/wr_cadmin_XXXXXX.db)
AJAR=$(mktemp -u /tmp/wr_cadmin_ajar_XXXXXX)

timeout 2 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1
sqlite3 "$DB" "INSERT INTO sites (slug,name,url,description,is_reachable,last_seen_at,owner,created_at) VALUES ('ops','Ops','https://ops.example','',1,9999999999,'dev:admin',7);"

timeout 15 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

curl -s -c "$AJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=admin"

api="http://127.0.0.1:$PORT/api/v1"
strip='s/"created_at":[0-9]*/"created_at":0/g'
ct='Content-Type: application/json'

echo "admin-post: $(curl -s -b "$AJAR" -X POST -H "$ct" -d '{"body":"admin note"}' "$api/comments/add")"
echo "list: $(curl -s "$api/comments" | sed "$strip")"
echo "admin-pending: $(curl -s -b "$AJAR" "$api/admin/comments" | sed "$strip")"

kill "$server" 2>/dev/null
rm -rf "$DB" "$AJAR"
