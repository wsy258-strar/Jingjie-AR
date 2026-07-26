#!/usr/bin/env bash
set -euo pipefail

# Start ar_server with AR_TEST_DB_DELAY_MS=200, then provide its PID, log and a
# disposable active session token.  This script never starts or stops services.
server_pid=${AR_SERVER_PID:?set AR_SERVER_PID to the test ar_server process}
server_log=${AR_SERVER_LOG:?set AR_SERVER_LOG to the test process log file}
host=${AR_TEST_HOST:-127.0.0.1}
port=${AR_TEST_PORT:?set AR_TEST_PORT}
token=${TEST_SESSION_TOKEN:?set TEST_SESSION_TOKEN}

kill -0 "$server_pid"
test -f "$server_log"
exec 3<>"/dev/tcp/$host/$port"
printf 'GET /api/session?token=%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n' \
  "$token" "$host" >&3
exec 3>&-
exec 3<&-

sleep 1
kill -0 "$server_pid"
if rg -i 'use-after-free|double-send|addresssanitizer|undefinedbehavior' "$server_log"; then
  echo 'unsafe disconnected-client handling found in server log' >&2
  exit 1
fi
echo 'PASS: disconnected async client'
