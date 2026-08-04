#!/usr/bin/env bash
set -euo pipefail

base_url="${BASE_URL:-http://127.0.0.1:8080}"

curl -fsS "$base_url/" | grep -Fq '境界AR · 360° 全景探索'
curl -fsS "$base_url/" | grep -Fq 'id="panorama-loading"'
curl -fsS "$base_url/vendor/aframe-1.6.0.min.js" | grep -Fq 'AFRAME'
curl -fsS "$base_url/css/style.css" | grep -Fq '.scene-card img { position:absolute; inset:0;'
curl -fsS "$base_url/css/panorama-loading.css" | grep -Fq 'pointer-events: none;'
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
for file in docklands_02 golden_bay graaff_reinet_groote_kerk illovo_beach_balcony \
            little_paris_eiffel_tower san_giuseppe_bridge venetian_crossroads vignaioli; do
    curl -fsSI "$base_url/assets/panoramas-preview/${file}_2048.webp" | grep -qi '^Content-Type: image/webp'
done
rg -Fq 'JsonUtil::escape(scene.previewUrl) << "\",\"thumbnail_url\":\""' WebApps/ARServer/src/handlers/SceneInteractionHandlers.cpp
rg -Fq 'output << ",\"like_count\":" << likes << "}";' WebApps/ARServer/src/handlers/SceneInteractionHandlers.cpp
curl -fsS "$base_url/js/app.js" | grep -Fq 'var previewUrl = scene.preview_url || scene.panorama_url;'
curl -fsS "$base_url/js/app.js" | grep -Fq 'function createPanoramaAssetImage(assetId, sourceUrl) {'
curl -fsS "$base_url/js/app.js" | grep -Fq "assets.appendChild(previewImage);"
curl -fsS "$base_url/js/app.js" | grep -Fq "sky.setAttribute('src', '#' + previewAssetId);"
curl -fsS "$base_url/js/app.js" | grep -Fq 'var loader = new THREE.TextureLoader();'
curl -fsS "$base_url/js/app.js" | grep -Fq 'loader.load(scene.panorama_url, function (texture) {'
curl -fsS "$base_url/js/app.js" | grep -Fq 'texture.colorSpace = THREE.SRGBColorSpace;'
curl -fsS "$base_url/js/app.js" | grep -Fq 'texture.encoding = THREE.sRGBEncoding;'
curl -fsS "$base_url/js/app.js" | grep -Fq "var mesh = previewSky.getObject3D('mesh');"
curl -fsS "$base_url/js/app.js" | grep -Fq 'mesh.material.map = texture;'
curl -fsS "$base_url/js/app.js" | grep -Fq 'if (sceneEpoch !== epoch || currentScene !== scene || !previewSky.parentNode) { texture.dispose(); return; }'
curl -fsS "$base_url/js/app.js" | grep -Fq 'var PREVIEW_MIN_DISPLAY_MS = 5000;'
curl -fsS "$base_url/js/app.js" | grep -Fq 'function setPanoramaLoading(visible, epoch) {'
curl -fsS "$base_url/js/app.js" | grep -Fq 'setPanoramaLoading(true, epoch);'
curl -fsS "$base_url/js/app.js" | grep -Fq 'setPanoramaLoading(false, epoch);'
curl -fsS "$base_url/js/app.js" | grep -Fq "localStorage.removeItem('jingjie.token');"
curl -fsS "$base_url/js/app.js" | grep -Fq "if (error.status === 401) { clearExpiredLogin(); showToast('登录已失效，请重新登录'); openAuthModal(); return; }"
curl -fsS "$base_url/js/app.js" | grep -Fq 'function applyHighResolutionTexture(scene, previewSky, epoch, texture) {'
curl -fsS "$base_url/js/app.js" | grep -Fq 'var remainingDelay = Math.max(0, PREVIEW_MIN_DISPLAY_MS - (Date.now() - previewReadyAt));'
curl -fsS "$base_url/js/app.js" | grep -Fq 'window.setTimeout(function () { applyHighResolutionTexture(scene, previewSky, epoch, texture); }, remainingDelay);'
curl -fsS "$base_url/js/app.js" | grep -Fq 'function beginPanoramaPreview(scene, assets, sky, epoch, previewUrl, previewAssetId) {'
curl -fsS "$base_url/js/app.js" | grep -Fq 'beginPanoramaPreview(scene, assets, sky, epoch, previewUrl, previewAssetId);'
