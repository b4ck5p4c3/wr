#!/usr/bin/env bash
# An admin adds a site directly, edits its name and url, then soft deletes it, so
# the ring reflects the add and the edit and then drops the row from the listing.
set -u
PORT=18771
DB=$(mktemp -u /tmp/wr_adm_XXXXXX.db)
WEB=$(mktemp -d)
AJAR=$(mktemp -u /tmp/wr_adm_jar_XXXXXX)
printf '<!doctype html><title>wr</title>' > "$WEB/index.html"

timeout 15 "$BIN" --dev --listen "http://127.0.0.1:$PORT" -d "$DB" -w "$WEB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

curl -s -c "$AJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=admin"
echo "add-direct: $(curl -s -b "$AJAR" -X POST -H 'Content-Type: application/json' -d '{"slug":"d","name":"D","url":"https://d.example","description":"first desc"}' "http://127.0.0.1:$PORT/api/v1/admin/site/add")"
echo "sites: $(curl -s "http://127.0.0.1:$PORT/sites" | sed 's/"created_at":[0-9]*/"created_at":0/g')"
echo "edit: $(curl -s -b "$AJAR" -X POST -H 'Content-Type: application/json' -d '{"slug":"d","name":"D2","url":"https://d2.example","description":"second desc"}' "http://127.0.0.1:$PORT/api/v1/admin/site")"
echo "sites-after-edit: $(curl -s "http://127.0.0.1:$PORT/sites" | sed 's/"created_at":[0-9]*/"created_at":0/g')"
echo "delete: $(curl -s -b "$AJAR" -X DELETE -H 'Content-Type: application/json' -d '{"slug":"d"}' "http://127.0.0.1:$PORT/api/v1/admin/site/delete")"
echo "sites-after-delete: $(curl -s "http://127.0.0.1:$PORT/sites")"

kill "$server" 2>/dev/null
rm -rf "$WEB" "$DB" "$AJAR"
