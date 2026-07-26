#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/json_helpers.sh"

base_url=${BASE_URL:?set BASE_URL, for example http://127.0.0.1:8080}
suffix="$(date +%s)-$$"
username="ar-api-${suffix}"
password="test-password-${suffix}"

auth=$(curl --fail-with-body -sS -X POST "$base_url/api/auth?username=$username&password=$password")
token=$(printf '%s' "$auth" | json_get session_token)
printf '%s' "$auth" | json_check 'data.get("status") == "ok" and isinstance(data.get("user_id"), int) and isinstance(data.get("is_new"), bool)'

for scene in 1 2 3 4 5; do
  curl --fail-with-body -sS -X POST "$base_url/api/session/enter?token=$token&scene=$scene" |
    json_check "data.get('status') == 'ok' and data.get('scene_id') == '$scene'"
done
curl --fail-with-body -sS "$base_url/api/session?token=$token" |
  json_check 'all(key in data for key in ("session_id", "user_id", "token", "scene_id", "status_code"))'
curl --fail-with-body -sS -X POST "$base_url/api/session/heartbeat?token=$token&scene=5" |
  json_check 'data.get("status") == "ok"'
curl --fail-with-body -sS "$base_url/api/scenes/5/members?token=$token" |
  json_check 'isinstance(data.get("members"), list)'
curl --fail-with-body -sS -X POST "$base_url/api/session/exit?token=$token" | json_check 'data.get("status") == "ok"'
status=$(curl -sS -o /tmp/ar-api-repeat-exit.$$ -w '%{http_code}' -X POST "$base_url/api/session/exit?token=$token")
trap 'rm -f /tmp/ar-api-repeat-exit.$$' EXIT
test "$status" = 409
test "$(curl -sS -o /dev/null -w '%{http_code}' -X OPTIONS -H 'Origin: https://example.test' -H 'Access-Control-Request-Method: GET' "$base_url/api/scenes")" = 204
test "$(curl -sS -o /dev/null -w '%{http_code}' "$base_url/no-such-route")" = 404
test "$(curl -sS -o /dev/null -w '%{http_code}' -X POST "$base_url/api/scenes")" = 405
test "$(curl -sS -o /dev/null -w '%{http_code}' -X POST "$base_url/api/scenes/1/interactions?token=$token")" = 501
echo 'PASS: AR API compatibility'
