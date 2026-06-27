#!/usr/bin/env bash
# The static asset handler serves the embedded frontend. The root and a client
# route return the html shell, the docs page and its openapi source are served
# from the bundle, an unknown path answers 404, and a path that carries a parent
# traversal is refused.
set -u
PORT=18782
DB=$(mktemp -u /tmp/wr_static_XXXXXX.db)

timeout 15 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

base="http://127.0.0.1:$PORT"
echo "root: $(curl -s -o /dev/null -w '%{http_code} %{content_type}' "$base/")"
echo "route: $(curl -s -o /dev/null -w '%{http_code} %{content_type}' "$base/about")"
echo "docs: $(curl -s -o /dev/null -w '%{http_code} %{content_type}' "$base/docs")"
echo "docs-body: $(curl -s "$base/docs" | grep -c '<h1>wr API documentation</h1>')"
echo "openapi: $(curl -s -o /dev/null -w '%{http_code} %{content_type}' "$base/openapi.yaml")"
echo "openapi-body: $(curl -s "$base/openapi.yaml" | grep -c '^openapi: 3.1.0')"
echo "emoji: $(curl -s -o /dev/null -w '%{http_code} %{content_type}' "$base/emoji/fire.png")"
echo "favicon: $(curl -s -o /dev/null -w '%{http_code} %{content_type}' "$base/favicon.ico")"
echo "unknown: $(curl -s -o /dev/null -w '%{http_code} %{content_type}' "$base/no/such/page")"
echo "traversal: $(curl -s --path-as-is -o /dev/null -w '%{http_code}' "$base/x/../y")"

kill "$server" 2>/dev/null
rm -rf "$DB"
