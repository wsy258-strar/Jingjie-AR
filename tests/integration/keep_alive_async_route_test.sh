#!/usr/bin/env bash
set -euo pipefail

# Exercises the browser sequence: load a static page, then issue an async API
# request on the same HTTP/1.1 persistent connection.
base_url=${BASE_URL:?set BASE_URL, for example http://127.0.0.1:8080}
username=${AUTH_USERNAME:?set AUTH_USERNAME to a disposable test user}
password=${AUTH_PASSWORD:?set AUTH_PASSWORD for the disposable test user}
tmp=$(mktemp -d /tmp/http-keep-alive-async.XXXXXX)
trap 'rm -rf "$tmp"' EXIT

curl --http1.1 --fail-with-body --silent --show-error \
  -o /dev/null "$base_url/" \
  --next -X POST "$base_url/api/auth?username=$username&password=$password" \
  -D "$tmp/headers" -o "$tmp/body"

rg -qi '^Content-Type: application/json; charset=utf-8\r?$' "$tmp/headers"
rg -q '^\{"status":"ok"' "$tmp/body"
echo 'PASS: keep-alive async route response'
