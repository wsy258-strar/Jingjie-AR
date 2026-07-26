# A-Frame 全景缩放 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让境界AR的全景查看器支持鼠标滚轮和手机双指捏合缩放。

**Architecture:** 在 `app.js` 中创建 A-Frame 场景后，取得默认相机对象并以受限 FOV 作为唯一缩放状态。输入事件绑定至 `aframe-host`，场景销毁时由同一清理函数解除绑定，因此不改变 A-Frame 的单指转向和陀螺仪控制。

**Tech Stack:** 原生 JavaScript、A-Frame 1.6、Bash 静态集成测试。

## Global Constraints

- 不引入第三方依赖，也不修改后端 API。
- FOV 默认值为 80，允许范围固定为 35–100。
- 仅双指触摸触发缩放，单指手势保留给 A-Frame `look-controls`。
- 每项改动先写失败测试、确认失败、最小实现、回归测试并单独提交。

---

### Task 1: 实现相机 FOV 缩放与输入生命周期

**Files:**
- Modify: `WebApps/ARServer/www/js/app.js:56-63`
- Modify: `tests/integration/jingjie_ar_static_test.sh:6-7`

**Interfaces:**
- Consumes: `createAFrameScene(scene)` 创建的 `a-scene`、`aframe-host` 元素和 A-Frame 默认 `[camera]` 实体。
- Produces: `bindPanoramaZoom(aframeScene)` 和 `clearPanoramaZoom()`，前者绑定滚轮与双指事件，后者销毁监听器与手势状态。

- [ ] **Step 1: 写失败的静态页面集成测试**

在 `tests/integration/jingjie_ar_static_test.sh` 的 A-Frame 资源断言后加入：

```bash
curl -fsS "$base_url/js/app.js" | grep -Fq 'var MIN_PANORAMA_FOV = 35, MAX_PANORAMA_FOV = 100, DEFAULT_PANORAMA_FOV = 80;'
curl -fsS "$base_url/js/app.js" | grep -Fq "host.addEventListener('wheel', onWheel, { passive: false });"
curl -fsS "$base_url/js/app.js" | grep -Fq "host.addEventListener('touchmove', onTouchMove, { passive: false });"
curl -fsS "$base_url/js/app.js" | grep -Fq 'clearPanoramaZoom();'
```

- [ ] **Step 2: 运行测试并确认失败**

运行：

```bash
BASE_URL=http://127.0.0.1:8080 bash tests/integration/jingjie_ar_static_test.sh
```

预期：失败；当前 `app.js` 不包含 FOV 常量与触摸监听器。

- [ ] **Step 3: 写最小实现**

在 `app.js` 顶部状态变量后加入下列代码。`updateFov` 使用 `Math.max`/`Math.min` 限制范围，并调用 `updateProjectionMatrix()` 使 Three.js 相机立即生效。双指距离增加时降低 FOV（放大），距离减少时提高 FOV（缩小）。

```js
var MIN_PANORAMA_FOV = 35, MAX_PANORAMA_FOV = 100, DEFAULT_PANORAMA_FOV = 80;
var panoramaZoomCleanup = null;

function clearPanoramaZoom() { if (panoramaZoomCleanup) panoramaZoomCleanup(); panoramaZoomCleanup = null; }
function bindPanoramaZoom(aframeScene) {
    clearPanoramaZoom();
    var host = $('aframe-host'), lastDistance = 0, camera = aframeScene.camera;
    function updateFov(delta) { if (!camera) return; camera.fov = Math.max(MIN_PANORAMA_FOV, Math.min(MAX_PANORAMA_FOV, camera.fov + delta)); camera.updateProjectionMatrix(); }
    function onWheel(event) { event.preventDefault(); updateFov(event.deltaY * 0.04); }
    function distance(touches) { var x = touches[0].clientX - touches[1].clientX, y = touches[0].clientY - touches[1].clientY; return Math.sqrt(x * x + y * y); }
    function onTouchStart(event) { if (event.touches.length === 2) lastDistance = distance(event.touches); }
    function onTouchMove(event) { if (event.touches.length !== 2 || !lastDistance) return; var current = distance(event.touches); updateFov((lastDistance - current) * 0.12); lastDistance = current; event.preventDefault(); }
    function onTouchEnd(event) { if (event.touches.length < 2) lastDistance = 0; }
    host.addEventListener('wheel', onWheel, { passive: false });
    host.addEventListener('touchstart', onTouchStart, { passive: true });
    host.addEventListener('touchmove', onTouchMove, { passive: false });
    host.addEventListener('touchend', onTouchEnd);
    panoramaZoomCleanup = function () { host.removeEventListener('wheel', onWheel); host.removeEventListener('touchstart', onTouchStart); host.removeEventListener('touchmove', onTouchMove); host.removeEventListener('touchend', onTouchEnd); };
}
```

将 `createAFrameScene` 改为在 `host.appendChild(aframeScene)` 后监听场景 `loaded` 事件：

```js
aframeScene.addEventListener('loaded', function () { if (aframeScene.camera) { aframeScene.camera.fov = DEFAULT_PANORAMA_FOV; aframeScene.camera.updateProjectionMatrix(); bindPanoramaZoom(aframeScene); } });
```

并把 `destroyAFrameScene` 改为：

```js
function destroyAFrameScene() { clearPanoramaZoom(); $('aframe-host').textContent = ''; }
```

- [ ] **Step 4: 运行静态集成测试**

运行：

```bash
BASE_URL=http://127.0.0.1:8080 bash tests/integration/jingjie_ar_static_test.sh
```

预期：退出码为 0。随后手动打开一个场景，验证滚轮上/下、双指张开/合拢、单指拖动和退出后再次进入。

- [ ] **Step 5: 提交任务**

```bash
git add WebApps/ARServer/www/js/app.js
git add -f tests/integration/jingjie_ar_static_test.sh
git commit -m "feat: add panorama gesture zoom"
```
