#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/json_helpers.sh"

test "${ALLOW_SERVICE_CONTROL:-}" = 1 || { echo 'set ALLOW_SERVICE_CONTROL=1 for the test Redis instance' >&2; exit 2; }
base_url=${BASE_URL:?set BASE_URL}
redis_port=${REDIS_PORT:?set REDIS_PORT for a disposable Redis instance}
stop_command=${REDIS_STOP_COMMAND:?set REDIS_STOP_COMMAND for the disposable Redis instance}
start_command=${REDIS_START_COMMAND:?set REDIS_START_COMMAND for the disposable Redis instance}
redis_running=1
restore_redis() {
  if test "$redis_running" = 0; then
    bash -c "$start_command"
  fi
}
trap restore_redis EXIT
suffix="$(date +%s)-$$"
auth=$(curl --fail-with-body -sS -X POST "$base_url/api/auth?username=fallback-$suffix&password=test-$suffix")
token=$(printf '%s' "$auth" | json_get session_token)
redis-cli -p "$redis_port" DEL "session:$token" >/dev/null
curl --fail-with-body -sS "$base_url/api/session?token=$token" | json_check "data.get('token') == '$token'"
bash -c "$stop_command"
redis_running=0
curl --fail-with-body -sS "$base_url/api/session?token=$token" | json_check "data.get('token') == '$token'"
members_status=$(curl -sS -o /dev/null -w '%{http_code}' "$base_url/api/scenes/1/members?token=$token")
test "$members_status" = 503
echo 'PASS: Redis session fallback and outage degradation'
