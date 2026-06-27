#!/usr/bin/env bash
# The error paths each answer with the right status. An unknown endpoint is 404,
# a mutation reached through GET is 405, an admin route without a session is
# refused, the current account without a session is 401, and an unknown slug is
# 404.
set -u
PORT=18768
DB=$(mktemp -u /tmp/wr_err_XXXXXX.db)

timeout 15 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

echo "unknown-endpoint: $(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/v1/nope")"
echo "mutation-get: $(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/v1/sites/add")"
echo "admin-unauth: $(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/v1/admin/pending")"
echo "me-unauth: $(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/v1/me")"
echo "unknown-slug: $(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/zzz")"

kill "$server" 2>/dev/null
rm -rf "$DB"
