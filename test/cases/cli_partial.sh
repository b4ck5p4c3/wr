#!/usr/bin/env bash
# A partial set lists only the still-missing options.
"$BIN" --database-url /tmp/wr_test_partial.db
echo "exit=$?"
