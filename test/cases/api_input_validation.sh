#!/usr/bin/env bash
# The add and the rename paths reject a bad slug, a bad name, a bad url, and a
# short description with a 400, so a malformed field never reaches the store.
set -u
PORT=18781
DB=$(mktemp -u /tmp/wr_valid_XXXXXX.db)
UJAR=$(mktemp -u /tmp/wr_valid_ujar_XXXXXX)

timeout 15 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"
curl -s -c "$UJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=user"

post() { curl -s -b "$UJAR" -X POST -H 'Content-Type: application/json' -d "$2" "http://127.0.0.1:$PORT/api/v1/$1"; }

echo "add-bad-slug: $(post sites/add '{"slug":"Bad_Slug","name":"n","url":"https://a.example","description":"long enough desc"}')"
echo "add-long-slug: $(post sites/add '{"slug":"aaaaaaaaaaaaaaaaa","name":"n","url":"https://a.example","description":"long enough desc"}')"
echo "add-long-name: $(post sites/add '{"slug":"ok","name":"this name is too long","url":"https://a.example","description":"long enough desc"}')"
echo "add-empty-name: $(post sites/add '{"slug":"ok","name":"","url":"https://a.example","description":"long enough desc"}')"
echo "add-bad-url: $(post sites/add '{"slug":"ok","name":"n","url":"ftp://a.example","description":"long enough desc"}')"
echo "add-scheme-only-url: $(post sites/add '{"slug":"ok","name":"n","url":"https://","description":"long enough desc"}')"
echo "add-short-desc: $(post sites/add '{"slug":"ok","name":"n","url":"https://a.example","description":"short"}')"
echo "rename-empty-name: $(post sites/rename '{"slug":"ok","name":""}')"
echo "rename-long-name: $(post sites/rename '{"slug":"ok","name":"this name is too long"}')"

kill "$server" 2>/dev/null
rm -rf "$DB" "$UJAR"
