#!/usr/bin/env bash
# An admin clears the server statement cache, an anonymous caller is refused, and
# a wrong method is rejected, so the cache clear endpoint is admin only and POST
# only.
set -u
PORT=18777
DB=$(mktemp -u /tmp/wr_cache_XXXXXX.db)
AJAR=$(mktemp -u /tmp/wr_cache_jar_XXXXXX)

timeout 15 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

curl -s -c "$AJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=admin"
echo "clear: $(curl -s -b "$AJAR" -X POST "http://127.0.0.1:$PORT/api/v1/admin/cache/clear")"
echo "anon: $(curl -s -X POST "http://127.0.0.1:$PORT/api/v1/admin/cache/clear")"
echo "wrong-method: $(curl -s "http://127.0.0.1:$PORT/api/v1/admin/cache/clear")"

kill "$server" 2>/dev/null
rm -rf "$DB" "$AJAR"
