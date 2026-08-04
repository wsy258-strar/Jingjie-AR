#!/usr/bin/env bash
set -euo pipefail

base_url="${BASE_URL:-http://127.0.0.1:8080}"
username="auth-body-${RANDOM}"
password="body-secret-${RANDOM}"

json_response=$(curl -fsS -X POST "$base_url/api/auth" \
    -H 'Content-Type: application/json' \
    --data "{\"username\":\"$username\",\"password\":\"$password\"}")
printf '%s' "$json_response" | grep -F '"status":"ok"'

query_response=$(curl -fsS -X POST "$base_url/api/auth?username=${username}&password=${password}")
printf '%s' "$query_response" | grep -F '"status":"ok"'

if [[ -n "${LOG_FILE:-}" ]]; then
    ! grep -F "$password" "$LOG_FILE"
fi
