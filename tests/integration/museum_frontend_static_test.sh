#!/usr/bin/env bash
set -euo pipefail

root="${1:-WebApps/ARServer/www}"
index="$root/index.html"
css="$root/css/museum.css"
app="$root/js/museum-app.js"

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

grep -Fq 'id="museum-fullscreen-root"' "$index"
grep -Fq 'id="museum-shell"' "$index"
grep -Fq 'id="scene-drawer-toggle"' "$index"
grep -Fq 'aria-controls="scene-drawer"' "$index"
grep -Fq 'aria-expanded="false"' "$index"
grep -Fq 'id="scene-drawer"' "$index"
grep -Fq 'id="fullscreen-toggle"' "$index"
grep -Fq 'id="music-toggle"' "$index"
grep -Fq 'id="vr-toggle"' "$index"
grep -Fq 'id="view-toggle"' "$index"
grep -Fq 'id="view-panel"' "$index"
grep -Fq 'data-view-mode="normal"' "$index"
grep -Fq 'data-view-mode="planet"' "$index"
grep -Fq 'data-view-mode="fisheye"' "$index"
grep -Fq 'data-view-mode="crystal"' "$index"
grep -Fq 'aria-label="全屏浏览"' "$index"
grep -Fq 'aria-label="当前场景暂无音乐"' "$index"
grep -Fq 'aria-label="进入 VR"' "$index"
grep -Fq 'aria-label="视角切换"' "$index"
grep -Fq '<svg' "$index"
! grep -Fq 'class="catalog-panel"' "$index"
! grep -Fq 'id="scene-title"' "$index"
! grep -Fq '360° PANORAMA' "$index"
! grep -Fq '>全屏浏览</button>' "$index"
! grep -Fq '>播放讲解</button>' "$index"

python3 - "$index" <<'PY'
from html.parser import HTMLParser
from pathlib import Path
import sys

VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"}

class ParentAudit(HTMLParser):
    def __init__(self):
        super().__init__()
        self.stack = []
        self.parents = {}

    def handle_starttag(self, tag, attrs):
        node_id = dict(attrs).get("id")
        if node_id:
            self.parents[node_id] = next((entry_id for _, entry_id in reversed(self.stack) if entry_id), None)
        if tag not in VOID:
            self.stack.append((tag, node_id))

    def handle_endtag(self, tag):
        while self.stack:
            open_tag, _ = self.stack.pop()
            if open_tag == tag:
                return

audit = ParentAudit()
audit.feed(Path(sys.argv[1]).read_text(encoding="utf-8"))
for child in ("museum-shell", "description-modal", "artwork-modal", "login-modal", "notice", "fatal-error"):
    assert audit.parents.get(child) == "museum-fullscreen-root", (child, audit.parents.get(child))
PY

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

grep -Fq 'import { MuseumUiState } from "./museum-ui-state.js"' "$app"
grep -Fq 'scene-drawer-toggle' "$app"
grep -Fq 'view-toggle' "$app"
grep -Fq 'data-view-mode' "$app"
grep -Fq 'fullscreenchange' "$app"
grep -Fq 'element("museum-fullscreen-root")' "$app"
grep -Fq 'adapter.setViewMode' "$app"
grep -Fq 'adapter.enterVr' "$app"
grep -Fq 'pointerdown' "$app"
grep -Fq 'wheel' "$app"
grep -Fq 'event.key === "Escape"' "$app"
! grep -Fq 'element("scene-title")' "$app"
! grep -Fq 'element("scene-count")' "$app"

grep -Fq '.museum-fullscreen-root' "$css"
grep -Fq '.museum-shell' "$css"
grep -Fq 'height: 100dvh' "$css"
grep -Fq '.floating-header' "$css"
grep -Fq 'backdrop-filter: blur(' "$css"
grep -Fq '.viewer-toolbar' "$css"
grep -Fq '.scene-drawer' "$css"
grep -Fq '.scene-drawer.is-open' "$css"
grep -Fq '.view-panel' "$css"
grep -Fq '@media (max-width: 820px)' "$css"
grep -Fq 'grid-template-areas:' "$css"
grep -Fq 'overflow-x: auto' "$css"
grep -Fq '@media (max-height: 420px) and (orientation: landscape)' "$css"
grep -Fq 'grid-template-columns: repeat(4, 38px)' "$css"
grep -Fq 'max-height: calc(100vh - 10rem)' "$css"
grep -Fq 'max-height: calc(100dvh - 10rem)' "$css"
grep -Fq 'overflow-y: auto' "$css"
grep -Fq '@media (prefers-reduced-motion: reduce)' "$css"
! grep -Fq 'grid-template-columns: minmax(0, 1fr) clamp(250px' "$css"
! grep -Fq '.catalog-panel' "$css"
! grep -Fq -- '--panel:' "$css"

printf 'PASS: museum frontend static shell\n'
