#!/usr/bin/env bash
set -euo pipefail

base_url="${BASE_URL:?set BASE_URL to an isolated running ar_server, for example http://127.0.0.1:8080}"
base_url="${base_url%/}"
tmp="$(mktemp -d /tmp/jingjie-museum-e2e.XXXXXX)"
trap 'rm -rf "$tmp"' EXIT
suffix="$(date +%s)-$$"

request() {
  local expected="$1" output="$2"
  shift 2
  local status
  status="$(curl --silent --show-error --output "$output" --write-out '%{http_code}' "$@")" || {
    printf 'request failed at step output %s\n' "$(basename "$output")" >&2
    return 1
  }
  if [ "$status" != "$expected" ]; then
    printf 'expected HTTP %s, got %s at step output %s\n' \
      "$expected" "$status" "$(basename "$output")" >&2
    return 1
  fi
}

json_value() {
  local file="$1" expression="$2"
  python3 - "$file" "$expression" <<'PY'
import json, sys
document = json.load(open(sys.argv[1], encoding="utf-8"))
value = eval(sys.argv[2], {"__builtins__": {}}, {"document": document})
if isinstance(value, bool):
    print("true" if value else "false")
elif value is None:
    print("null")
else:
    print(value)
PY
}

assert_success() {
  test "$(json_value "$1" 'document.get("success")')" = true
}

request 200 "$tmp/views-before.json" "$base_url/api/statistics/views"
assert_success "$tmp/views-before.json"
views_before="$(json_value "$tmp/views-before.json" 'document["data"]["totalViews"]')"
test "$views_before" != null || { echo 'isolated end-to-end test requires available view statistics' >&2; exit 1; }

bootstrap_id="museum-e2e-$suffix"
request 200 "$tmp/bootstrap-a.json" -X POST "$base_url/api/visitors/session" \
  -H 'Content-Type: application/json' --data "{\"bootstrapRequestId\":\"$bootstrap_id\"}"
assert_success "$tmp/bootstrap-a.json"
visitor_a="$(json_value "$tmp/bootstrap-a.json" 'document["data"]["visitorToken"]')"
views_after_a="$(json_value "$tmp/bootstrap-a.json" 'document["data"]["totalViews"]')"
test "$views_after_a" -eq "$((views_before + 1))"
printf 'PASS 1/9: 游客 A 初始化且浏览量 +1\n'

request 200 "$tmp/retry-a.json" -X POST "$base_url/api/visitors/session" \
  -H 'Content-Type: application/json' -H "X-Visitor-Token: $visitor_a" \
  --data "{\"bootstrapRequestId\":\"$bootstrap_id\"}"
request 200 "$tmp/views-retry.json" "$base_url/api/statistics/views"
test "$(json_value "$tmp/views-retry.json" 'document["data"]["totalViews"]')" -eq "$views_after_a"
test "$(json_value "$tmp/retry-a.json" 'document["data"]["visitorToken"]')" = "$visitor_a"
printf 'PASS 2/9: 相同请求 ID 重试未重复计数\n'

request 200 "$tmp/bootstrap-b.json" -X POST "$base_url/api/visitors/session" \
  -H 'Content-Type: application/json' --data "{\"bootstrapRequestId\":\"museum-e2e-b-$suffix\"}"
visitor_b="$(json_value "$tmp/bootstrap-b.json" 'document["data"]["visitorToken"]')"
test "$visitor_b" != "$visitor_a"
request 200 "$tmp/presence-two.json" "$base_url/api/presence"
test "$(json_value "$tmp/presence-two.json" 'document["data"]["onlineCount"]')" -eq 2
printf 'PASS 3/9: 游客 B 初始化后展馆在线人数为 2\n'

request 200 "$tmp/catalog.json" "$base_url/api/scenes"
python3 - "$tmp/catalog.json" "$base_url" "$tmp" <<'PY'
import json, pathlib, sys, urllib.request
catalog = json.load(open(sys.argv[1], encoding="utf-8"))["data"]
scene_ids = [scene["sceneId"] for scene in catalog["scenes"]]
assert len(scene_ids) >= 2
details = []
for index, scene_id in enumerate(scene_ids):
    with urllib.request.urlopen(f"{sys.argv[2]}/api/scenes/{scene_id}") as response:
        document = json.load(response)
    assert document["success"] is True
    details.append(document["data"])
    pathlib.Path(sys.argv[3], f"scene-{index}.json").write_text(
        json.dumps(document, ensure_ascii=False), encoding="utf-8")
owners = {}
for scene in details:
    for hotspot in scene.get("hotspots", []):
        artwork_id = hotspot.get("artworkId")
        if artwork_id:
            owners.setdefault(artwork_id, set()).add(scene["sceneId"])
shared = next(((artwork_id, sorted(scenes)) for artwork_id, scenes in owners.items()
               if len(scenes) >= 2), None)
assert shared, "目录中没有被两个场景热点共同引用的作品"
pathlib.Path(sys.argv[3], "shared.txt").write_text(
    shared[0] + "\n" + shared[1][0] + "\n" + shared[1][1] + "\n", encoding="utf-8")
PY
mapfile -t shared < "$tmp/shared.txt"
artwork_id="${shared[0]}"
first_scene="${shared[1]}"
second_scene="${shared[2]}"
request 200 "$tmp/scene-switch.json" "$base_url/api/scenes/$second_scene"
request 200 "$tmp/heartbeat-a.json" -X POST "$base_url/api/presence/heartbeat" \
  -H "X-Visitor-Token: $visitor_a"
request 200 "$tmp/presence-after-switch.json" "$base_url/api/presence"
test "$(json_value "$tmp/presence-after-switch.json" 'document["data"]["onlineCount"]')" -eq 2
printf 'PASS 4/9: A 切换场景后展馆在线人数仍为 2\n'

request 200 "$tmp/shared-link.html" --get --data-urlencode "artwork=$artwork_id" "$base_url/"
grep -Fq 'src="/js/museum-app.js"' "$tmp/shared-link.html"
printf 'PASS 5/9: 带作品参数的分享链接可加载展馆页面\n'

request 401 "$tmp/guest-like.json" -X POST "$base_url/api/artworks/$artwork_id/likes"
test "$(json_value "$tmp/guest-like.json" 'document["success"]')" = false
printf 'PASS 6/9: 未登录作品写接口返回 401\n'

username="museum-e2e-$suffix"
password="museum-e2e-password-$suffix"
request 200 "$tmp/auth.json" -X POST "$base_url/api/auth" \
  -H 'Content-Type: application/json' \
  --data "{\"username\":\"$username\",\"password\":\"$password\"}"
user_token="$(json_value "$tmp/auth.json" 'document["data"]["token"]')"
request 200 "$tmp/like.json" -X POST "$base_url/api/artworks/$artwork_id/likes" \
  -H "Authorization: Bearer $user_token"
request 200 "$tmp/comment.json" -X POST "$base_url/api/artworks/$artwork_id/comments" \
  -H "Authorization: Bearer $user_token" -H 'Content-Type: application/json' \
  --data "{\"content\":\"端到端验收-$suffix\"}"
assert_success "$tmp/like.json"
assert_success "$tmp/comment.json"
test "$(json_value "$tmp/like.json" 'document["data"]["liked"]')" = true
test "$(json_value "$tmp/comment.json" 'document["data"]["commentId"]')" -gt 0
printf 'PASS 7/9: 登录后点赞和评论成功\n'

request 200 "$tmp/shared-scene-first.json" "$base_url/api/scenes/$first_scene"
request 200 "$tmp/shared-scene-second.json" "$base_url/api/scenes/$second_scene"
first_hotspot_artwork="$(python3 - "$tmp/shared-scene-first.json" "$artwork_id" <<'PY'
import json, sys
scene = json.load(open(sys.argv[1], encoding="utf-8"))["data"]
matches = [hotspot["artworkId"] for hotspot in scene["hotspots"]
           if hotspot.get("artworkId") == sys.argv[2]]
assert matches, f"场景 {scene['sceneId']} 未引用作品 {sys.argv[2]}"
print(matches[0])
PY
)"
second_hotspot_artwork="$(python3 - "$tmp/shared-scene-second.json" "$artwork_id" <<'PY'
import json, sys
scene = json.load(open(sys.argv[1], encoding="utf-8"))["data"]
matches = [hotspot["artworkId"] for hotspot in scene["hotspots"]
           if hotspot.get("artworkId") == sys.argv[2]]
assert matches, f"场景 {scene['sceneId']} 未引用作品 {sys.argv[2]}"
print(matches[0])
PY
)"
test "$first_hotspot_artwork" = "$second_hotspot_artwork"
request 200 "$tmp/detail-first.json" "$base_url/api/artworks/$first_hotspot_artwork" \
  -H "Authorization: Bearer $user_token"
request 200 "$tmp/comments-first.json" "$base_url/api/artworks/$first_hotspot_artwork/comments"
request 200 "$tmp/detail-second.json" "$base_url/api/artworks/$second_hotspot_artwork" \
  -H "Authorization: Bearer $user_token"
request 200 "$tmp/comments-second.json" "$base_url/api/artworks/$second_hotspot_artwork/comments"
cmp "$tmp/detail-first.json" "$tmp/detail-second.json"
cmp "$tmp/comments-first.json" "$tmp/comments-second.json"
grep -Fq "端到端验收-$suffix" "$tmp/comments-first.json"
printf 'PASS 8/9: 两个场景热点读取到相同作品互动数据\n'

request 200 "$tmp/exit-a.json" -X POST "$base_url/api/presence/exit" \
  -H "X-Visitor-Token: $visitor_a"
request 200 "$tmp/presence-one.json" "$base_url/api/presence"
test "$(json_value "$tmp/presence-one.json" 'document["data"]["onlineCount"]')" -eq 1
printf 'PASS 9/9: A 退出后展馆在线人数为 1\n'

printf 'PASS: 展馆全链路 9 步验收完成\n'
