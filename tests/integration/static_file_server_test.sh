#!/usr/bin/env bash
set -euo pipefail

base_url=${BASE_URL:?set BASE_URL to a running ar_server}
tmp=$(mktemp -d /tmp/ar-static-download.XXXXXX)
trap 'rm -rf "$tmp"' EXIT
curl --fail-with-body -sS "$base_url/" -o "$tmp/index.html"
cmp WebApps/ARServer/www/index.html "$tmp/index.html"
curl --fail-with-body -sS "$base_url/css/style.css" -o "$tmp/style.css"
cmp WebApps/ARServer/www/css/style.css "$tmp/style.css"
echo 'PASS: static resources'
