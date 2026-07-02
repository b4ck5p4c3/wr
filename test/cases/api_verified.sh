#!/usr/bin/env bash
# The public listing carries owner_is_verified for every site, unset for an
# owner outside the configured organisation.
set -u
PORT=18782
DB=$(mktemp -u /tmp/wr_verified_XXXXXX.db)
UJAR=$(mktemp -u /tmp/wr_verified_ujar_XXXXXX)
AJAR=$(mktemp -u /tmp/wr_verified_ajar_XXXXXX)

timeout 15 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

curl -s -c "$UJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=user"
curl -s -b "$UJAR" -X POST -H 'Content-Type: application/json' -d '{"slug":"v","name":"V","url":"https://v.example","description":"a verified test"}' -o /dev/null "http://127.0.0.1:$PORT/api/v1/sites/add"

curl -s -c "$AJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=admin"
id=$(curl -s -b "$AJAR" "http://127.0.0.1:$PORT/api/v1/admin/pending" | grep -o '"id":[0-9]*' | head -1 | grep -o '[0-9]*')
curl -s -b "$AJAR" -X POST -H 'Content-Type: application/json' -d '{"id":'$id'}' -o /dev/null "http://127.0.0.1:$PORT/api/v1/admin/pending/approve"

echo "sites: $(curl -s "http://127.0.0.1:$PORT/sites" | sed 's/"created_at":[0-9]*/"created_at":0/g')"
echo "has-verified-field: $(curl -s "http://127.0.0.1:$PORT/sites" | grep -c '"owner_is_verified":false')"

kill "$server" 2>/dev/null
rm -rf "$DB" "$UJAR" "$AJAR"
