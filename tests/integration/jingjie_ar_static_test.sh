#!/usr/bin/env bash
set -euo pipefail

base_url="${BASE_URL:-http://127.0.0.1:8080}"

curl -fsS "$base_url/" | grep -Fq '境界AR · 360° 全景探索'
curl -fsS "$base_url/vendor/aframe-1.6.0.min.js" | grep -Fq 'AFRAME'
curl -fsS "$base_url/css/style.css" | grep -Fq '.scene-card img { position:absolute; inset:0;'
curl -fsS "$base_url/js/app.js" | grep -Fq 'var MIN_PANORAMA_FOV = 35, MAX_PANORAMA_FOV = 100, DEFAULT_PANORAMA_FOV = 80;'
curl -fsS "$base_url/js/app.js" | grep -Fq "host.addEventListener('wheel', onWheel, { passive: false });"
curl -fsS "$base_url/js/app.js" | grep -Fq "host.addEventListener('touchmove', onTouchMove, { passive: false });"
curl -fsS "$base_url/js/app.js" | grep -Fq 'clearPanoramaZoom();'
curl -fsS "$base_url/js/app.js" | grep -Fq "aframeScene.camera.el.setAttribute('look-controls', 'reverseMouseDrag: true');"
curl -fsS "$base_url/js/app.js" | grep -Fq "aframeScene.setAttribute('xr-mode-ui', 'enabled: false');"
for file in docklands_02_8k golden_bay_8k graaff_reinet_groote_kerk_8k \
            illovo_beach_balcony_8k little_paris_eiffel_tower_8k san_giuseppe_bridge_16k \
            venetian_crossroads_16k vignaioli_16k; do
    curl -fsSI "$base_url/assets/panoramas/$file.webp" | grep -qi '^Content-Type: image/webp'
done
