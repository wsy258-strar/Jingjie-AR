#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/json_helpers.sh"
base_url=${BASE_URL:?set BASE_URL, for example http://127.0.0.1:8080}
suffix="$(date +%s)-$$"

register_user() {
  local name="$1" response
  response=$(curl --fail-with-body -sS -X POST "$base_url/api/auth" -H 'Content-Type: application/json' \
    --data "{\"username\":\"$name\",\"password\":\"jingjie-${suffix}\"}")
  printf '%s' "$response" | json_get session_token
}

guest_status=$(curl -sS -o /dev/null -w '%{http_code}' -X POST "$base_url/api/scenes/golden-bay/likes")
test "$guest_status" = 401
echo 'guest write rejected'
token_a=$(register_user "jingjie-a-${suffix}")
token_b=$(register_user "jingjie-b-${suffix}")

curl --fail-with-body -sS -X POST "$base_url/api/session/enter?token=$token_a&scene=golden-bay" | json_check 'data.get("status") == "ok"'
curl --fail-with-body -sS -X POST "$base_url/api/session/enter?token=$token_b&scene=golden-bay" | json_check 'data.get("status") == "ok"'
curl --fail-with-body -sS -X POST "$base_url/api/session/heartbeat?token=$token_a&scene=golden-bay" | json_check 'data.get("status") == "ok"'
curl --fail-with-body -sS -X POST "$base_url/api/session/heartbeat?token=$token_b&scene=golden-bay" | json_check 'data.get("status") == "ok"'
echo 'two users entered and heartbeated'
curl --fail-with-body -sS -X POST "$base_url/api/scenes/golden-bay/likes" -H "Authorization: Bearer $token_a" | json_check 'data.get("status") == "ok" and data.get("liked") is True'
curl --fail-with-body -sS -X POST "$base_url/api/scenes/golden-bay/comments" -H "Authorization: Bearer $token_a" -H 'Content-Type: application/json' --data '{"content":"沉浸感很好"}' | json_check 'data.get("status") == "ok"'
curl --fail-with-body -sS "$base_url/api/scenes/golden-bay/comments" | json_check 'any(x.get("content") == "沉浸感很好" for x in data.get("comments", []))'
echo 'like and comment persisted'
curl --fail-with-body -sS "$base_url/api/scenes/golden-bay/members" | json_check 'len(data.get("members", [])) == 2'
curl --fail-with-body -sS -X POST "$base_url/api/session/exit?token=$token_b" | json_check 'data.get("status") == "ok"'
echo 'PASS: Jingjie AR interactions'
