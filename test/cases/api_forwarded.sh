#!/usr/bin/env bash
# Under --trust-forwarded-headers the rate limiter keys on the forwarded client
# address. The comment endpoint caps an address at three an hour, so a fourth
# post from the same forwarded address is throttled while a different address
# starts fresh. The rightmost x-forwarded-for hop and the x-real-ip header are
# both honored, and a padded x-real-ip maps to the same bucket as the bare one.
set -u
PORT=18783
DB=$(mktemp -u /tmp/wr_fwd_XXXXXX.db)

WR_SESSION_KEY=test-key WR_GITHUB_CLIENT_ID=id WR_GITHUB_CLIENT_SECRET=secret \
  timeout 15 "$BIN" --listen-address "http://127.0.0.1:$PORT" -d "$DB" \
  -u http://x --trust-forwarded-headers >/dev/null 2>&1 &
server=$!
disown
curl -s --retry 60 --retry-connrefused --retry-delay 0 -o /dev/null "http://127.0.0.1:$PORT/api/v1/config"

api="http://127.0.0.1:$PORT/api/v1"
ct='Content-Type: application/json'
post() {
  curl -s -o /dev/null -w '%{http_code}' -X POST -H "$ct" "$@" \
    -d '{"body":"hi"}' "$api/comments/add"
}

for i in 1 2 3 4; do
  echo "xff-a-$i: $(post -H 'X-Forwarded-For: 1.1.1.1')"
done
echo "xff-b: $(post -H 'X-Forwarded-For: 2.2.2.2')"

for i in 1 2 3 4; do
  echo "real-$i: $(post -H 'X-Real-IP: 3.3.3.3')"
done
echo "real-padded: $(post -H 'X-Real-IP:   3.3.3.3   ')"

kill "$server" 2>/dev/null
rm -rf "$DB"
