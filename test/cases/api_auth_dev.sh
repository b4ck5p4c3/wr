#!/usr/bin/env bash
# The dev login bypass opens a session for an admin, the current account is read
# back through it, and logout closes it so the account is no longer reachable.
set -u
PORT=18767
DB=$(mktemp -u /tmp/wr_auth_XXXXXX.db)
WEB=$(mktemp -d)
JAR=$(mktemp -u /tmp/wr_auth_jar_XXXXXX)
printf '<!doctype html><title>wr</title>' > "$WEB/index.html"

timeout 15 "$BIN" --dev --listen "http://127.0.0.1:$PORT" -d "$DB" -w "$WEB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/config"

echo "admin-login: $(curl -s -c "$JAR" -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/auth/dev?role=admin")"
echo "admin-me: $(curl -s -b "$JAR" "http://127.0.0.1:$PORT/api/me")"
echo "logout: $(curl -s -b "$JAR" -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:$PORT/auth/logout")"
echo "after-logout-me: $(curl -s -b "$JAR" -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/me")"

kill "$server" 2>/dev/null
rm -rf "$WEB" "$DB" "$JAR"
