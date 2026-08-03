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

for id in artwork-gallery-stage artwork-image artwork-prev artwork-next \
  artwork-image-count artwork-zoom-in artwork-zoom-out artwork-reset \
  artwork-image-status artwork-details-panel artwork-comments-panel; do
  grep -Fq "id=\"$id\"" "$index"
done

python3 - "$index" <<'PY'
from html.parser import HTMLParser
from pathlib import Path
import sys

class ToolIconAudit(HTMLParser):
    def __init__(self):
        super().__init__()
        self.current_button = None
        self.buttons = {}

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "button" and attrs.get("id") in {
            "artwork-zoom-in", "artwork-zoom-out", "artwork-reset"
        }:
            self.current_button = attrs["id"]
            assert self.current_button not in self.buttons
            self.buttons[self.current_button] = {
                "aria_label": attrs.get("aria-label"),
                "svgs": [],
                "text": "",
            }
        elif tag == "svg" and self.current_button:
            self.buttons[self.current_button]["svgs"].append({
                "class": attrs.get("class"),
                "aria_hidden": attrs.get("aria-hidden"),
            })

    def handle_data(self, data):
        if self.current_button:
            self.buttons[self.current_button]["text"] += data.strip()

    def handle_endtag(self, tag):
        if tag == "button":
            self.current_button = None

audit = ToolIconAudit()
audit.feed(Path(sys.argv[1]).read_text(encoding="utf-8"))
assert audit.buttons == {
    "artwork-zoom-in": {
        "aria_label": "放大图片",
        "svgs": [{
            "class": "artwork-tool-icon artwork-tool-icon-zoom-in",
            "aria_hidden": "true",
        }],
        "text": "放大",
    },
    "artwork-zoom-out": {
        "aria_label": "缩小图片",
        "svgs": [{
            "class": "artwork-tool-icon artwork-tool-icon-zoom-out",
            "aria_hidden": "true",
        }],
        "text": "缩小",
    },
    "artwork-reset": {
        "aria_label": "重置图片视图",
        "svgs": [{
            "class": "artwork-tool-icon artwork-tool-icon-reset",
            "aria_hidden": "true",
        }],
        "text": "重置",
    },
}
PY

python3 - "$css" <<'PY'
from pathlib import Path
import re
import sys

css = Path(sys.argv[1]).read_text(encoding="utf-8")

def braced_content(source, opening_brace):
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace + 1:index]
    raise AssertionError("unclosed CSS block")

def selector_block(source, selector):
    match = re.search(rf"(?m)^[ \t]*{re.escape(selector)}[ \t]*\{{", source)
    assert match, f"missing selector block: {selector}"
    return braced_content(source, match.end() - 1)

def media_block(condition):
    match = re.search(rf"@media\s*{re.escape(condition)}\s*\{{", css)
    assert match, f"missing media query: {condition}"
    return braced_content(css, match.end() - 1)

def declarations(block):
    return {
        name: value.strip()
        for name, value in re.findall(r"([\w-]+)\s*:\s*([^;{}]+);", block)
    }

def assert_properties(label, block, expected):
    actual = declarations(block)
    for name, value in expected.items():
        assert actual.get(name) == value, (
            f"{label}: expected {name}: {value}; got {actual.get(name)!r}"
        )
    return actual

desktop_tools = selector_block(css, ".artwork-gallery-tools")
desktop_tool_values = assert_properties(
    "desktop toolbar",
    desktop_tools,
    {
        "position": "absolute",
        "right": ".75rem",
        "bottom": ".75rem",
        "display": "flex",
    },
)
assert "top" not in desktop_tool_values, "desktop toolbar must not use top positioning"
assert_properties(
    "desktop toolbar button",
    selector_block(css, ".artwork-gallery-tools button"),
    {
        "display": "inline-flex",
        "align-items": "center",
        "font-size": ".78rem",
        "gap": ".28rem",
    },
)
assert_properties(
    "desktop toolbar icon",
    selector_block(css, ".artwork-tool-icon"),
    {
        "width": "15px",
        "height": "15px",
        "fill": "none",
        "stroke": "var(--accent-strong)",
    },
)
assert_properties(
    "desktop image status",
    selector_block(css, ".artwork-image-status"),
    {"left": ".75rem", "right": "auto", "bottom": ".75rem"},
)

short_landscape = "(max-width: 900px) and (max-height: 420px) and (orientation: landscape)"
assert css.index(f"@media {short_landscape}") < css.index("@media (max-width: 520px)"), (
    "narrow mobile overrides must follow short landscape overrides"
)
short_media = media_block(short_landscape)
assert_properties(
    "short landscape artwork layout",
    selector_block(short_media, ".artwork-layout"),
    {"grid-template-rows": "minmax(130px, 2fr) auto minmax(0, 3fr)"},
)
assert_properties(
    "short landscape gallery navigation",
    selector_block(short_media, ".artwork-gallery-nav"),
    {"top": ".45rem", "width": "32px", "height": "32px", "transform": "none"},
)
assert_properties(
    "short landscape image count",
    selector_block(short_media, ".artwork-image-count"),
    {"top": ".45rem", "left": "50%", "transform": "translateX(-50%)"},
)

for label, condition, status_bottom in (
    ("short landscape", short_landscape, ".45rem"),
    ("narrow mobile", "(max-width: 520px)", "calc(.45rem + 2.3rem)"),
):
    media = media_block(condition)
    assert_properties(
        f"{label} toolbar",
        selector_block(media, ".artwork-gallery-tools"),
        {"right": ".45rem", "bottom": ".45rem", "gap": ".2rem", "padding": ".2rem"},
    )
    assert_properties(
        f"{label} toolbar button",
        selector_block(media, ".artwork-gallery-tools button"),
        {"gap": ".2rem", "font-size": ".72rem", "min-height": "28px"},
    )
    assert_properties(
        f"{label} image status",
        selector_block(media, ".artwork-image-status"),
        {"left": ".45rem", "right": "auto", "bottom": status_bottom},
    )
PY

grep -Fq 'role="tablist"' "$index"
grep -Fq 'data-artwork-tab="details"' "$index"
grep -Fq 'data-artwork-tab="comments"' "$index"

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
grep -Fq '@media (max-width: 820px), (max-width: 900px) and (max-height: 420px) and (orientation: landscape)' "$css"
grep -Fq '@media (max-width: 900px) and (max-height: 420px) and (orientation: landscape)' "$css"
grep -Fq '.artwork-interaction-dock textarea {' "$css"
grep -Fq 'min-height: 2.6rem;' "$css"
grep -Fq 'grid-template-areas:' "$css"
grep -Fq 'overflow-x: auto' "$css"
grep -Fq '@media (max-height: 420px) and (orientation: landscape)' "$css"
grep -Fq 'grid-template-columns: repeat(4, 38px)' "$css"
grep -Fq 'max-height: calc(100vh - 10rem)' "$css"
grep -Fq 'max-height: calc(100dvh - 10rem)' "$css"
grep -Fq 'overflow-y: auto' "$css"
grep -Fq 'grid-template-rows: minmax(0, 3fr) minmax(0, 2fr)' "$css"
grep -Fq '.artwork-interaction-dock' "$css"
grep -Fq '.artwork-comments-scroll' "$css"
grep -Fq '.artwork-card.is-text-only .artwork-gallery,' "$css"
grep -Fq '.artwork-card.is-text-only .artwork-tabs,' "$css"
grep -Fq '.artwork-card.is-text-only .artwork-interactions {' "$css"
grep -Fq 'display: none !important;' "$css"
grep -Fq '.artwork-card.is-text-only .artwork-content { display: block; }' "$css"
grep -Fq '.artwork-card.is-text-only .artwork-content { display: contents; }' "$css"
grep -Fq '.artwork-card.is-text-only .artwork-details-panel { grid-row: 1 / 4; }' "$css"
grep -Fq '@media (prefers-reduced-motion: reduce)' "$css"
! grep -Fq 'grid-template-columns: minmax(0, 1fr) clamp(250px' "$css"
! grep -Fq '.catalog-panel' "$css"
! grep -Fq -- '--panel:' "$css"

printf 'PASS: museum frontend static shell\n'
