#!/usr/bin/env bash
# Seed a fixed ring, then exercise the public navigation API. The server launch
# is wrapped in a timeout so a blocking accept never freezes the run, and the
# sites are seeded with a far-future last_seen so the liveness sweep leaves them
# alone and the output stays deterministic.
set -u
PORT=18765
DB=$(mktemp -u /tmp/wr_api_XXXXXX.db)
WEB=$(mktemp -d)
printf '<!doctype html><title>wr</title>' > "$WEB/index.html"

# Create the schema, then seed.
timeout 2 "$BIN" --listen "http://127.0.0.1:$PORT" -d "$DB" -w "$WEB" -u http://x >/dev/null 2>&1
sqlite3 "$DB" "INSERT INTO sites (slug,name,url,favicon,is_reachable,last_seen_at,owner,created_at) VALUES ('a','Site A','https://a.example','',1,9999999999,'x',1),('b','Site B','https://b.example','',1,9999999999,'x',2);"

timeout 15 "$BIN" --listen "http://127.0.0.1:$PORT" -d "$DB" -w "$WEB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/sites"

echo "sites: $(curl -s "http://127.0.0.1:$PORT/sites")"
echo "a-data: $(curl -s "http://127.0.0.1:$PORT/a/data")"
echo "a-next: $(curl -s -o /dev/null -w '%{http_code} %{redirect_url}' "http://127.0.0.1:$PORT/a/next")"
echo "me-unauth: $(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/me")"

kill "$server" 2>/dev/null
rm -rf "$WEB" "$DB"
