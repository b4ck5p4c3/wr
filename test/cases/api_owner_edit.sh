#!/usr/bin/env bash
# An owner edits the name, url, and description of a site they own, the edit
# waits for review, an admin approves it, and the ring shows the new fields. A
# signed-in non-owner is refused the edit.
set -u
PORT=18780
DB=$(mktemp -u /tmp/wr_edit_XXXXXX.db)
UJAR=$(mktemp -u /tmp/wr_edit_ujar_XXXXXX)
AJAR=$(mktemp -u /tmp/wr_edit_ajar_XXXXXX)

timeout 15 "$BIN" --enable-dangerous-developer-environment --listen-address "http://127.0.0.1:$PORT" -d "$DB" -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

curl -s -c "$UJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=user"
curl -s -c "$AJAR" -o /dev/null "http://127.0.0.1:$PORT/auth/dev?role=admin"

user_post() { curl -s -b "$UJAR" -X POST -H 'Content-Type: application/json' -d "$2" "http://127.0.0.1:$PORT/api/v1/$1"; }
admin_post() { curl -s -b "$AJAR" -X POST -H 'Content-Type: application/json' -d "$2" "http://127.0.0.1:$PORT/api/v1/$1"; }
approve_first() {
  id=$(curl -s -b "$AJAR" "http://127.0.0.1:$PORT/api/v1/admin/pending" | grep -o '"id":[0-9]*' | head -1 | grep -o '[0-9]*')
  admin_post admin/pending/approve '{"id":'"$id"'}'
}

user_post sites/add '{"slug":"mine","name":"My Site","url":"https://mine.example","description":"a personal site"}' >/dev/null
approve_first >/dev/null

echo "edit-submit: $(user_post sites/rename '{"slug":"mine","name":"Edited","url":"https://edited.example","description":"an edited description"}')"
echo "edit-not-owner: $(admin_post sites/rename '{"slug":"mine","name":"Hijack","url":"https://hijack.example","description":"a hijack attempt"}')"
echo "edit-approve: $(approve_first)"
echo "sites-after-edit: $(curl -s "http://127.0.0.1:$PORT/sites" | sed 's/"created_at":[0-9]*/"created_at":0/g')"

kill "$server" 2>/dev/null
rm -rf "$DB" "$UJAR" "$AJAR"
