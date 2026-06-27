#!/usr/bin/env bash
# A signed-in user toggles emoji reactions on a site, the public listing carries
# the counts, the signed-in listing carries the caller's own reactions, and an
# anonymous react is refused.
set -u
PORT=18773
DB=$(mktemp -u /tmp/wr_react_XXXXXX.db)
UJAR=$(mktemp -u /tmp/wr_react_jar_XXXXXX)

timeout 15 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

sqlite3 "$DB" "INSERT INTO sites (slug,name,url,description,is_reachable,last_seen_at,owner,created_at) VALUES ('rx','RX','https://rx.example','',1,9999999999,'x',7);"
curl -s -c "$UJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=user"

echo "react-fire: $(curl -s -b "$UJAR" -X POST -H 'Content-Type: application/json' -d '{"slug":"rx","emoji":"fire"}' "http://127.0.0.1:$PORT/api/v1/sites/react")"
echo "react-star: $(curl -s -b "$UJAR" -X POST -H 'Content-Type: application/json' -d '{"slug":"rx","emoji":"star"}' "http://127.0.0.1:$PORT/api/v1/sites/react")"
echo "react-bad: $(curl -s -b "$UJAR" -X POST -H 'Content-Type: application/json' -d '{"slug":"rx","emoji":"taco"}' "http://127.0.0.1:$PORT/api/v1/sites/react")"
echo "react-missing: $(curl -s -b "$UJAR" -X POST -H 'Content-Type: application/json' -d '{"slug":"ghost","emoji":"fire"}' "http://127.0.0.1:$PORT/api/v1/sites/react")"
echo "react-unauth: $(curl -s -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' -d '{"slug":"rx","emoji":"fire"}' "http://127.0.0.1:$PORT/api/v1/sites/react")"
echo "sites-anon: $(curl -s "http://127.0.0.1:$PORT/sites")"
echo "sites-user: $(curl -s -b "$UJAR" "http://127.0.0.1:$PORT/sites")"
echo "toggle-off: $(curl -s -b "$UJAR" -X POST -H 'Content-Type: application/json' -d '{"slug":"rx","emoji":"fire"}' "http://127.0.0.1:$PORT/api/v1/sites/react")"
echo "sites-after: $(curl -s -b "$UJAR" "http://127.0.0.1:$PORT/sites")"

kill "$server" 2>/dev/null
rm -rf "$DB" "$UJAR"
