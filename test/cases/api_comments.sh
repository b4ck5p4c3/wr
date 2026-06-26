#!/usr/bin/env bash
# The footer comments are open to an owner of a site in the ring. A user who owns
# a seeded site posts and the comment is listed, the admin who owns nothing is
# refused, an anonymous post is rejected, and an empty body is rejected.
set -u
PORT=18774
DB=$(mktemp -u /tmp/wr_comment_XXXXXX.db)
WEB=$(mktemp -d)
UJAR=$(mktemp -u /tmp/wr_comment_ujar_XXXXXX)
AJAR=$(mktemp -u /tmp/wr_comment_ajar_XXXXXX)
printf '<!doctype html><title>wr</title>' > "$WEB/index.html"

timeout 2 "$BIN" --dev --listen "http://127.0.0.1:$PORT" -d "$DB" -w "$WEB" -u http://x >/dev/null 2>&1
sqlite3 "$DB" "INSERT INTO sites (slug,name,url,description,is_reachable,last_seen_at,owner,created_at) VALUES ('mine','Mine','https://mine.example','',1,9999999999,'dev:user',7);"

timeout 15 "$BIN" --dev --listen "http://127.0.0.1:$PORT" -d "$DB" -w "$WEB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

curl -s -c "$UJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=user"
curl -s -c "$AJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=admin"

echo "post-owner: $(curl -s -b "$UJAR" -X POST -H 'Content-Type: application/json' -d '{"body":"hello @mine and @ghost"}' "http://127.0.0.1:$PORT/api/v1/comments/add")"
echo "post-nonowner: $(curl -s -b "$AJAR" -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' -d '{"body":"hi"}' "http://127.0.0.1:$PORT/api/v1/comments/add")"
echo "post-anon: $(curl -s -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' -d '{"body":"hi"}' "http://127.0.0.1:$PORT/api/v1/comments/add")"
echo "post-empty: $(curl -s -b "$UJAR" -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' -d '{"body":""}' "http://127.0.0.1:$PORT/api/v1/comments/add")"
echo "list: $(curl -s "http://127.0.0.1:$PORT/api/v1/comments" | sed 's/"created_at":[0-9]*/"created_at":0/g')"

kill "$server" 2>/dev/null
rm -rf "$WEB" "$DB" "$UJAR" "$AJAR"
