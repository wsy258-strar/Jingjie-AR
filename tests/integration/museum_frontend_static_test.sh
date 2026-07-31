#!/usr/bin/env bash
set -euo pipefail

root="${1:-WebApps/ARServer/www}"
index="$root/index.html"

grep -Fq 'type="module"' "$index"
grep -Fq '/assets/krp/runtime/player_krp_v2.js' "$index"
grep -Fq 'id="museum-title"' "$index"
grep -Fq 'id="museum-description"' "$index"
grep -Fq 'id="scene-catalog"' "$index"
grep -Fq 'id="panorama"' "$index"
grep -Fq 'id="total-views"' "$index"
grep -Fq 'id="online-count"' "$index"
grep -Fq 'id="artwork-modal"' "$index"
grep -Fq 'id="login-modal"' "$index"
grep -Fq 'id="notice"' "$index"

! grep -R -i -E '720yun|api\.map|amap|panoOffline\.js|aframe' \
  "$index" "$root/js" "$root/css"

test ! -e "$root/js/app.js"
test ! -e "$root/css/style.css"
test ! -e "$root/css/panorama-loading.css"
test ! -e "$root/vendor/aframe-1.6.0.min.js"
test ! -e "$root/assets/panoramas"
test ! -e "$root/assets/panoramas-preview"
test ! -e "$root/assets/thumbnail"

grep -Fq 'new AbortController' "$root/js/museum-app.js"
grep -Fq '.abort()' "$root/js/museum-app.js"
grep -Fq 'textContent' "$root/js/artwork-modal.js"

printf 'PASS: museum frontend static shell\n'
