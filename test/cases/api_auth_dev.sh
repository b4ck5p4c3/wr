#!/usr/bin/env bash
# The dev login bypass opens a session for an admin, the current account is read
# back through it, and logout closes it so the account is no longer reachable.
set -u
PORT=18767
DB=$(mktemp -u /tmp/wr_auth_XXXXXX.db)
JAR=$(mktemp -u /tmp/wr_auth_jar_XXXXXX)

WR_SESSION_KEY=test-key timeout 15 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

echo "admin-login: $(curl -s -c "$JAR" -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/auth/dev?role=admin")"
token=$(awk '$6 == "wr_session" { print $7 }' "$JAR")
case "$token" in
  *.*) echo "session-signed: 1" ;;
  *) echo "session-signed: 0" ;;
esac
echo "admin-me: $(curl -s -b "$JAR" "http://127.0.0.1:$PORT/api/v1/me")"
tampered_token="${token%?}x"
echo "tampered-me: $(curl -s -H "Cookie: wr_session=$tampered_token" -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/v1/me")"

kill "$server" 2>/dev/null
wait "$server" 2>/dev/null
PORT=18797
WR_SESSION_KEY=other-key timeout 15 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

echo "rotated-me: $(curl -s -b "$JAR" -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/v1/me")"
echo "rotated-login: $(curl -s -c "$JAR" -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/auth/dev?role=admin")"
echo "logout: $(curl -s -b "$JAR" -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:$PORT/auth/logout")"
echo "after-logout-me: $(curl -s -b "$JAR" -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/v1/me")"

kill "$server" 2>/dev/null
rm -rf "$DB" "$JAR"
