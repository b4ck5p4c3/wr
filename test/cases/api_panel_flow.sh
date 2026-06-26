#!/usr/bin/env bash
# A user submits a site, it waits for review and is absent from the ring, the
# user sees it pending, then an admin approves it and it joins the ring.
set -u
PORT=18770
DB=$(mktemp -u /tmp/wr_panel_XXXXXX.db)
WEB=$(mktemp -d)
UJAR=$(mktemp -u /tmp/wr_panel_ujar_XXXXXX)
AJAR=$(mktemp -u /tmp/wr_panel_ajar_XXXXXX)
printf '<!doctype html><title>wr</title>' > "$WEB/index.html"

timeout 15 "$BIN" --dev --listen "http://127.0.0.1:$PORT" -d "$DB" -w "$WEB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/config"

curl -s -c "$UJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=user"
echo "add: $(curl -s -b "$UJAR" -X POST -H 'Content-Type: application/json' -d '{"slug":"mine","name":"My Site","url":"https://mine.example","description":"a personal site"}' "http://127.0.0.1:$PORT/api/sites/add")"
echo "sites-before: $(curl -s "http://127.0.0.1:$PORT/sites")"
echo "me-pending: $(curl -s -b "$UJAR" "http://127.0.0.1:$PORT/api/me")"

curl -s -c "$AJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=admin"
id=$(curl -s -b "$AJAR" "http://127.0.0.1:$PORT/api/admin/pending" | grep -o '"id":[0-9]*' | head -1 | grep -o '[0-9]*')
payload='{"id":'$id'}'
echo "approve: $(curl -s -b "$AJAR" -X POST -H 'Content-Type: application/json' -d "$payload" "http://127.0.0.1:$PORT/api/admin/pending/approve")"
echo "sites-after: $(curl -s "http://127.0.0.1:$PORT/sites" | sed 's/"created_at":[0-9]*/"created_at":0/g')"

kill "$server" 2>/dev/null
rm -rf "$WEB" "$DB" "$UJAR" "$AJAR"
