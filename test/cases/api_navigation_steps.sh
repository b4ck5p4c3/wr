#!/usr/bin/env bash
# The navigation steps walk the ring. A prev hop lands on the wrapping neighbor,
# a random hop answers with a redirect, and the step data form returns the three
# neighbors. The sites carry a far-future last_seen so the liveness sweep leaves
# them alone and the output stays deterministic.
set -u
PORT=18769
DB=$(mktemp -u /tmp/wr_nav_XXXXXX.db)

timeout 2 "$BIN" --dev --listen "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1
sqlite3 "$DB" "INSERT INTO sites (slug,name,url,description,is_reachable,last_seen_at,owner,created_at) VALUES ('a','Site A','https://a.example','',1,9999999999,'x',1),('b','Site B','https://b.example','',1,9999999999,'x',2);"

timeout 15 "$BIN" --dev --listen "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/sites"

echo "a-prev: $(curl -s -o /dev/null -w '%{http_code} %{redirect_url}' "http://127.0.0.1:$PORT/a/prev")"
echo "a-random-status: $(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/a/random")"
echo "b-next-data: $(curl -s "http://127.0.0.1:$PORT/b/next/data")"

kill "$server" 2>/dev/null
rm -rf "$DB"
