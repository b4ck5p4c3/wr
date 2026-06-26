#!/usr/bin/env bash
# The config endpoint reports the dev state and the available providers. Dev mode
# is on and no provider is configured, so the output is deterministic.
set -u
PORT=18766
DB=$(mktemp -u /tmp/wr_cfg_XXXXXX.db)

timeout 15 "$BIN" --enable-dangerous-developer-environment --listen-on "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

echo "config: $(curl -s "http://127.0.0.1:$PORT/api/v1/config")"

kill "$server" 2>/dev/null
rm -rf "$DB"
