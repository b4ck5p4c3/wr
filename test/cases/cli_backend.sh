#!/usr/bin/env bash
# An unknown database backend is rejected before startup, while sqlite and
# postgresql are the accepted values.
"$BIN" --listen-address http://127.0.0.1:1 --database-url /tmp/wr_backend.db --database-backend mysql
echo "exit=$?"
