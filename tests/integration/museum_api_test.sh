#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"
ARTWORK_ID="${ARTWORK_ID:-s_76196995_2}"
AUTH_USERNAME="${AUTH_USERNAME:-museum_api_$(date +%s)_$$}"
AUTH_PASSWORD="${AUTH_PASSWORD:-MuseumApiTest_2026}"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT
response_body="$tmp_dir/body.json"

fail() {
  printf 'FAIL: %s\n' "$*" >&2
  if [[ -s "$response_body" ]]; then
    printf 'last response: ' >&2
    cat "$response_body" >&2
    printf '\n' >&2
  fi
  exit 1
}

request() {
  local expected_status="$1"
  shift
  local status
  status="$(curl --silent --show-error --connect-timeout 3 --max-time 10 \
    --output "$response_body" --write-out '%{http_code}' "$@")" ||
    fail "request failed: $*"
  [[ "$status" == "$expected_status" ]] ||
    fail "expected HTTP $expected_status, got $status: $*"
}

json_value() {
  local expression="$1"
  python3 - "$response_body" "$expression" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    value = json.load(stream)
for component in sys.argv[2].split("."):
    value = value[int(component)] if component.isdigit() else value[component]
if isinstance(value, bool):
    print("true" if value else "false")
elif value is None:
    print("null")
else:
    print(value)
PY
}

assert_success() {
  [[ "$(json_value success)" == "true" ]] || fail "response envelope is not successful"
}

request 200 "$BASE_URL/api/scenes"
assert_success
[[ -n "$(json_value data.defaultSceneId)" ]] || fail "default scene is missing"

bootstrap_id_1="museum-api-${RANDOM}-$$-1"
request 200 -X POST "$BASE_URL/api/visitors/session" \
  -H 'Content-Type: application/json' \
  --data "{\"bootstrapRequestId\":\"$bootstrap_id_1\"}"
assert_success
visitor_token="$(json_value data.visitorToken)"
views_1="$(json_value data.totalViews)"
[[ "$visitor_token" =~ ^[0-9a-f]{64}$ ]] || fail "visitor token has invalid syntax"
[[ "$views_1" =~ ^[0-9]+$ ]] || fail "initial total view count is unavailable"

# 同一页面初始化请求换用新的幂等 ID，因此浏览总量应再次递增且不按访客去重。
bootstrap_id_2="museum-api-${RANDOM}-$$-2"
request 200 -X POST "$BASE_URL/api/visitors/session" \
  -H 'Content-Type: application/json' \
  -H "X-Visitor-Token: $visitor_token" \
  --data "{\"bootstrapRequestId\":\"$bootstrap_id_2\"}"
assert_success
views_2="$(json_value data.totalViews)"
[[ "$views_2" =~ ^[0-9]+$ ]] || fail "refreshed total view count is unavailable"
(( views_2 == views_1 + 1 )) ||
  fail "refresh should increment views once ($views_1 -> $views_2)"

request 200 -X POST "$BASE_URL/api/presence/heartbeat" \
  -H "X-Visitor-Token: $visitor_token"
assert_success
request 200 "$BASE_URL/api/presence"
assert_success
(( $(json_value data.onlineCount) >= 1 )) || fail "visitor is not counted online"

request 200 -X POST "$BASE_URL/api/auth" \
  -H 'Content-Type: application/json' \
  --data "{\"username\":\"$AUTH_USERNAME\",\"password\":\"$AUTH_PASSWORD\"}"
assert_success
user_token="$(json_value data.token)"
[[ "$user_token" =~ ^[0-9a-f]{64}$ ]] || fail "user token has invalid syntax"
[[ "$(json_value data.userId)" =~ ^[0-9]+$ ]] || fail "userId is missing"

request 200 "$BASE_URL/api/artworks/$ARTWORK_ID" \
  -H "Authorization: Bearer $user_token"
assert_success
request 200 -X POST "$BASE_URL/api/artworks/$ARTWORK_ID/likes" \
  -H "Authorization: Bearer $user_token"
assert_success
[[ "$(json_value data.liked)" == "true" ]] || fail "artwork like was not persisted"

comment="museum api integration $$"
request 200 -X POST "$BASE_URL/api/artworks/$ARTWORK_ID/comments" \
  -H 'Content-Type: application/json' \
  -H "Authorization: Bearer $user_token" \
  --data "{\"content\":\"$comment\"}"
assert_success
[[ "$(json_value data.commentId)" =~ ^[0-9]+$ ]] || fail "commentId is missing"

request 200 "$BASE_URL/api/artworks/$ARTWORK_ID/comments?limit=20"
assert_success
python3 - "$response_body" "$comment" <<'PY' || fail "created comment is not listed"
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    comments = json.load(stream)["data"]["comments"]
raise SystemExit(0 if any(item.get("content") == sys.argv[2] for item in comments) else 1)
PY

request 200 -X POST "$BASE_URL/api/presence/exit" \
  -H "X-Visitor-Token: $visitor_token"
assert_success

printf 'PASS: museum API\n'
