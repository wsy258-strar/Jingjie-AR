#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/json_helpers.sh"
token=$(printf '{"session_token":"abc","status":"ok"}' | json_get session_token)
test "$token" = abc
printf '{"status":"ok","user_id":7}' | json_check 'data.get("status") == "ok" and isinstance(data.get("user_id"), int)'
printf '{"active":true,"members":[{"session_token":"abc"}]}' | json_check 'isinstance(data.get("active"), bool) and isinstance(data.get("members"), list) and all(member.get("session_token") for member in data["members"])'
printf '{"session_id":1,"user_id":2,"token":"abc","scene_id":5,"status_code":1}' | json_check 'all(key in data for key in ("session_id", "user_id", "token", "scene_id", "status_code"))'
echo 'PASS: JSON integration helpers'
