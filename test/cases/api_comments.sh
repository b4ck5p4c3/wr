#!/usr/bin/env bash
# The footer comments are open to an owner of a site in the ring. A user who owns
# a seeded site posts, a body with a swear word is refused, the admin who owns
# nothing is refused, an anonymous post is rejected, and an empty body is
# rejected. A posted comment is held pending until an admin approves it, and an
# admin deletes it afterward.
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

api="http://127.0.0.1:$PORT/api/v1"
strip='s/"created_at":[0-9]*/"created_at":0/g'
ct='Content-Type: application/json'

echo "post-owner: $(curl -s -b "$UJAR" -X POST -H "$ct" -d '{"body":"hello @mine and @ghost"}' "$api/comments/add")"
echo "post-swear: $(curl -s -b "$UJAR" -o /dev/null -w '%{http_code}' -X POST -H "$ct" -d '{"body":"this is shit"}' "$api/comments/add")"
echo "post-nonowner: $(curl -s -b "$AJAR" -o /dev/null -w '%{http_code}' -X POST -H "$ct" -d '{"body":"hi"}' "$api/comments/add")"
echo "post-anon: $(curl -s -o /dev/null -w '%{http_code}' -X POST -H "$ct" -d '{"body":"hi"}' "$api/comments/add")"
echo "post-empty: $(curl -s -b "$UJAR" -o /dev/null -w '%{http_code}' -X POST -H "$ct" -d '{"body":""}' "$api/comments/add")"
echo "list-pending: $(curl -s "$api/comments" | sed "$strip")"
echo "admin-pending: $(curl -s -b "$AJAR" "$api/admin/comments" | sed "$strip")"
echo "approve-nonadmin: $(curl -s -b "$UJAR" -o /dev/null -w '%{http_code}' -X POST -H "$ct" -d '{"id":1}' "$api/admin/comments/approve")"
echo "approve: $(curl -s -b "$AJAR" -X POST -H "$ct" -d '{"id":1}' "$api/admin/comments/approve")"
echo "list: $(curl -s "$api/comments" | sed "$strip")"
echo "list-with-limit: $(curl -s "$api/comments?offset=0&limit=10" | sed "$strip")"
curl -s -b "$UJAR" -X POST -H "$ct" -d '{"body":"the second note"}' "$api/comments/add" >/dev/null
curl -s -b "$AJAR" -X POST -H "$ct" -d '{"id":2}' "$api/admin/comments/approve" >/dev/null
echo "list-two: $(curl -s "$api/comments" | sed "$strip")"
echo "page-first: $(curl -s "$api/comments?offset=0&limit=1" | sed "$strip")"
echo "page-offset: $(curl -s "$api/comments?offset=1&limit=1" | sed "$strip")"
echo "approve-missing-id: $(curl -s -b "$AJAR" -o /dev/null -w '%{http_code}' -X POST -H "$ct" -d '{}' "$api/admin/comments/approve")"
echo "delete-missing-id: $(curl -s -b "$AJAR" -o /dev/null -w '%{http_code}' -X POST -H "$ct" -d '{}' "$api/admin/comments/delete")"
echo "approve-nonexistent: $(curl -s -b "$AJAR" -o /dev/null -w '%{http_code}' -X POST -H "$ct" -d '{"id":999}' "$api/admin/comments/approve")"
echo "delete-nonexistent: $(curl -s -b "$AJAR" -o /dev/null -w '%{http_code}' -X POST -H "$ct" -d '{"id":999}' "$api/admin/comments/delete")"
echo "delete: $(curl -s -b "$AJAR" -X POST -H "$ct" -d '{"id":1}' "$api/admin/comments/delete")"
echo "delete-2: $(curl -s -b "$AJAR" -X POST -H "$ct" -d '{"id":2}' "$api/admin/comments/delete")"
echo "list-after-delete: $(curl -s "$api/comments" | sed "$strip")"

kill "$server" 2>/dev/null
rm -rf "$WEB" "$DB" "$UJAR" "$AJAR"
