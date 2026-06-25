#!/usr/bin/env bash
# The log tail is admin only. A session-less request is refused and an admin
# request answers with a JSON array. The line contents carry timestamps, so only
# the status and the array shape are checked.
set -u
PORT=18772
DB=$(mktemp -u /tmp/wr_logs_XXXXXX.db)
WEB=$(mktemp -d)
AJAR=$(mktemp -u /tmp/wr_logs_jar_XXXXXX)
printf '<!doctype html><title>wr</title>' > "$WEB/index.html"

timeout 15 "$BIN" --dev --listen "http://127.0.0.1:$PORT" -d "$DB" -w "$WEB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/config"

echo "logs-unauth: $(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/admin/logs")"
curl -s -c "$AJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=admin"
echo "logs-status: $(curl -s -b "$AJAR" -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/admin/logs")"
body=$(curl -s -b "$AJAR" "http://127.0.0.1:$PORT/api/admin/logs")
if [[ "$body" == \[* ]]; then echo "logs-isarray: yes"; else echo "logs-isarray: no"; fi

kill "$server" 2>/dev/null
rm -rf "$WEB" "$DB" "$AJAR"
