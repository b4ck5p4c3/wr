#!/usr/bin/env bash
# The rate limiter throttles the api outside dev mode. The comment endpoint caps
# an address at three an hour, so a burst is allowed three times then answered
# with a 429. The limiter runs before the auth check, so the unsigned posts
# still count toward the cap.
set -u
PORT=18778
DB=$(mktemp -u /tmp/wr_rl_XXXXXX.db)

WR_SESSION_KEY=test-key WR_GITHUB_CLIENT_ID=id WR_GITHUB_CLIENT_SECRET=secret \
  timeout 15 "$BIN" --listen-address "http://127.0.0.1:$PORT" -d "$DB" \
  -u http://x >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

api="http://127.0.0.1:$PORT/api/v1"
ct='Content-Type: application/json'
for i in 1 2 3 4 5; do
  echo "post-$i: $(curl -s -o /dev/null -w '%{http_code}' -X POST -H "$ct" -d '{"body":"hi"}' "$api/comments/add")"
done

kill "$server" 2>/dev/null
rm -rf "$DB"
