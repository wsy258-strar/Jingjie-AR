#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/json_helpers.sh"

base_url=${BASE_URL:?set BASE_URL to a running ar_server}

suffix="$(date +%s)-$$"
scene=1
authenticate() {
  local name=$1
  curl --fail-with-body -sS -X POST \
    "$base_url/api/auth?username=$name&password=test-$suffix" |
    json_get session_token
}
enter_and_heartbeat() {
  local token=$1
  curl --fail-with-body -sS -X POST "$base_url/api/session/enter?token=$token&scene=$scene" |
    json_check "data.get('status') == 'ok' and data.get('scene_id') == '$scene'"
  curl --fail-with-body -sS -X POST "$base_url/api/session/heartbeat?token=$token&scene=$scene" |
    json_check 'data.get("status") == "ok"'
}
member_count() {
  curl --fail-with-body -sS "$base_url/api/scenes/$scene/members?token=$1" |
    python3 -c 'import json, sys; print(len(json.load(sys.stdin)["members"]))'
}

first=$(authenticate "collab-a-$suffix")
second=$(authenticate "collab-b-$suffix")
enter_and_heartbeat "$first"
enter_and_heartbeat "$second"
test "$(member_count "$first")" = 2

curl --fail-with-body -sS -X POST "$base_url/api/session/exit?token=$first" |
  json_check 'data.get("status") == "ok"'
test "$(member_count "$second")" = 1

# Presence TTL is intentionally 30 seconds; leave margin for worker scheduling
# and coarse CI timers before asserting that the stale member disappeared.
sleep 33
test "$(member_count "$second")" = 0
echo 'PASS: weak collaboration lifecycle'
