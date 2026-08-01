# 沉浸式全景展馆界面 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将现有数字展馆前端改造成全视口沉浸式全景界面，并提供可收起场景抽屉、图标工具栏、VR 和四种视角模式。

**Architecture:** 保留现有原生 HTML/CSS/JavaScript 轻量壳，将界面开关状态提取到纯 JavaScript `MuseumUiState`，将投影和 VR 能力集中到 `KrpanoAdapter`。`museum-app.js` 只负责把 DOM 事件、业务数据和两个独立模块连接起来，后端 API 与展馆配置格式保持不变。

**Tech Stack:** 原生 HTML5、CSS3、JavaScript ES Modules、Node.js `node:test`、Bash 静态集成测试、krpano 1.20.7 WebVR 插件

## Global Constraints

- 全部用户可见文案使用中文，简介入口必须显示为“展馆简介”。
- 全景画面覆盖整个浏览器视口，不保留固定右侧场景栏和左下角当前场景名称。
- 桌面端顶部使用三个独立悬浮玻璃分组；手机端使用两行悬浮布局，不删除功能。
- 右侧工具顺序固定为全屏、音乐、VR、视角切换，四个按钮只显示项目内嵌 SVG 图标，不显示常驻文字标签。
- 场景抽屉和视角浮层互斥；选择场景、操作全景、点击外部或按下 `Escape` 时关闭场景抽屉。
- 视角模式固定为 `normal`、`planet`、`fisheye`、`crystal`，切换场景后保持，刷新页面后恢复 `normal`。
- 不修改后端 API、数据库结构或 `WebApps/ARServer/config/exhibition.json` 格式。
- 不增加第三方 UI、字体图标或 CDN 依赖；VR 插件使用项目已有 krpano 1.20.7 配套文件。
- 工具操作失败时保留当前全景，仅通过现有 `notice` 显示中文提示。
- 所有新行为遵循测试先行；实现任务必须记录 RED 与 GREEN 命令结果。

---

## 文件职责

- `WebApps/ARServer/www/js/krpano-adapter.js`：场景加载、投影模式、VR 能力检测和播放器错误边界。
- `WebApps/ARServer/www/js/museum-ui-state.js`：场景抽屉与视角浮层的纯状态机，不直接访问 DOM。
- `WebApps/ARServer/www/js/museum-app.js`：展馆数据、DOM 渲染、工具事件和适配层协调。
- `WebApps/ARServer/www/index.html`：全屏全景壳、悬浮导航、图标工具栏、场景抽屉和业务弹窗结构。
- `WebApps/ARServer/www/css/museum.css`：沉浸式布局、玻璃组件、响应式、可访问性和动效。
- `WebApps/ARServer/www/assets/krp/plugins/webvr.js`：与现有播放器版本配套的本地 WebVR 插件。
- `tests/frontend/krpano-adapter.test.mjs`：适配层投影、场景恢复和 VR 行为测试。
- `tests/frontend/museum-ui-state.test.mjs`：临时浮层状态机测试。
- `tests/integration/museum_frontend_static_test.sh`：DOM、样式、图标、可访问属性和旧布局移除的静态契约。

---

### Task 1: krpano 视角模式与 VR 适配能力

**Files:**
- Modify: `WebApps/ARServer/www/js/krpano-adapter.js`
- Copy: `/home/wsy/workspace/Jingjie-AR/pano/html/assets/krp/1.20.7/plugins/webvr.js` → `WebApps/ARServer/www/assets/krp/plugins/webvr.js`
- Modify: `tests/frontend/krpano-adapter.test.mjs`

**Interfaces:**
- Produces: `VIEW_MODES: Readonly<Record<string, string>>`
- Produces: `KrpanoAdapter#setViewMode(mode: "normal" | "planet" | "fisheye" | "crystal"): string`
- Produces: `KrpanoAdapter#enterVr(): true`
- Produces: `KrpanoAdapter#isVrAvailable(): boolean`
- Preserves: `KrpanoAdapter#loadScene(scene, generation): Promise<boolean>`

- [ ] **Step 1: 写入视角模式、场景恢复和 VR 的失败测试**

在 `tests/frontend/krpano-adapter.test.mjs` 的解构赋值中加入 `VIEW_MODES`，并追加以下测试：

```javascript
test("导出四种稳定的语义视角模式", () => {
  assert.deepEqual(VIEW_MODES, {
    NORMAL: "normal",
    PLANET: "planet",
    FISHEYE: "fisheye",
    CRYSTAL: "crystal"
  });
});

test("四种视角模式映射为独立的 krpano 投影动作", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  const values = {
    "view.hlookat": "18",
    "view.vlookat": "-4",
    "view.fov": "72",
    "webvr.isavailable": true
  };
  const player = {
    get(key) { return values[key]; },
    call(command) { calls.push(command); }
  };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama" });
    await adapter.initialize();
    assert.equal(adapter.setViewMode("planet"), "planet");
    assert.match(calls.at(-1), /stereographic,true/);
    assert.match(calls.at(-1), /vlookat,90/);
    assert.equal(adapter.setViewMode("fisheye"), "fisheye");
    assert.match(calls.at(-1), /fisheye,1\.0/);
    assert.equal(adapter.setViewMode("crystal"), "crystal");
    assert.match(calls.at(-1), /stereographic,true/);
    assert.match(calls.at(-1), /vlookat,0/);
    assert.equal(adapter.setViewMode("normal"), "normal");
    assert.match(calls.at(-1), /stereographic,false/);
    assert.match(calls.at(-1), /lookto\(18,-4,72/);
    assert.throws(() => adapter.setViewMode("unknown"), /不支持的视角模式/);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("切换场景后重新应用当前特殊视角", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  const player = {
    get(key) {
      return { "view.hlookat": "0", "view.vlookat": "0", "view.fov": "90" }[key];
    },
    call(command) { calls.push(command); }
  };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama" });
    await adapter.loadScene(scene, 1);
    adapter.setViewMode("fisheye");
    await adapter.loadScene({ ...scene, sceneId: "76196993" }, 2);
    assert.match(calls.at(-1), /fisheye,1\.0/);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("VR 仅在播放器和插件可用时进入", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  let available = false;
  const player = {
    get(key) { return key === "webvr.isavailable" ? available : "0"; },
    call(command) { calls.push(command); }
  };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama" });
    assert.throws(() => adapter.enterVr(), /尚未就绪/);
    await adapter.initialize();
    assert.equal(adapter.isVrAvailable(), false);
    assert.throws(() => adapter.enterVr(), /当前设备或浏览器不支持 VR/);
    available = true;
    assert.equal(adapter.isVrAvailable(), true);
    assert.equal(adapter.enterVr(), true);
    assert.equal(calls.at(-1), "webvr.enterVR();");
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});
```

- [ ] **Step 2: 运行测试并确认 RED**

Run:

```bash
node --test tests/frontend/krpano-adapter.test.mjs
```

Expected: FAIL，首个新增失败明确指出 `VIEW_MODES` 未导出或 `setViewMode` 不存在。

- [ ] **Step 3: 实现最小适配能力**

在 `krpano-adapter.js` 顶部加入：

```javascript
export const VIEW_MODES = Object.freeze({
  NORMAL: "normal",
  PLANET: "planet",
  FISHEYE: "fisheye",
  CRYSTAL: "crystal"
});

const VIEW_ACTIONS = Object.freeze({
  normal: ({ hlookat, vlookat, fov }) =>
    `set(view.stereographic,false); tween(view.fisheye,0.0,0.35); lookto(${hlookat},${vlookat},${fov},smooth(45,45,60));`,
  planet: () =>
    "set(view.stereographic,true); tween(view.fisheye,1.0,0.45); tween(view.vlookat,90,0.45); tween(view.fov,150,0.45);",
  fisheye: () =>
    "set(view.stereographic,false); tween(view.fisheye,1.0,0.35); tween(view.fov,120,0.35);",
  crystal: () =>
    "set(view.stereographic,true); tween(view.fisheye,1.0,0.45); tween(view.vlookat,0,0.45); tween(view.fov,150,0.45);"
});
```

在构造函数中初始化：

```javascript
this.viewMode = VIEW_MODES.NORMAL;
this.normalView = null;
```

在类中增加：

```javascript
applyViewMode(mode) {
  const actionFactory = VIEW_ACTIONS[mode];
  if (!actionFactory) throw new Error(`不支持的视角模式：${mode}`);
  const fallback = this.normalView || this.getView() || { hlookat: 0, vlookat: 0, fov: 90 };
  this.player.call(actionFactory(fallback));
}

setViewMode(mode) {
  if (!this.player) throw new Error("全景播放器尚未就绪");
  if (!VIEW_ACTIONS[mode]) throw new Error(`不支持的视角模式：${mode}`);
  if (this.viewMode === VIEW_MODES.NORMAL && mode !== VIEW_MODES.NORMAL)
    this.normalView = this.getView();
  this.applyViewMode(mode);
  this.viewMode = mode;
  if (mode === VIEW_MODES.NORMAL) this.normalView = null;
  return mode;
}

isVrAvailable() {
  if (!this.player) return false;
  const available = this.player.get("webvr.isavailable");
  return available === true || available === "true";
}

enterVr() {
  if (!this.player) throw new Error("全景播放器尚未就绪");
  if (!this.isVrAvailable()) throw new Error("当前设备或浏览器不支持 VR");
  this.player.call("webvr.enterVR();");
  return true;
}
```

在 `buildSceneXml()` 生成的根节点后加入本地插件定义：

```javascript
'<plugin name="webvr" devices="html5" keep="true"',
' url="/assets/krp/plugins/webvr.js" mobilevr_support="true" />',
```

在 `loadScene()` 成功调用 `loadxml` 后、提交热点映射前加入：

```javascript
if (this.viewMode !== VIEW_MODES.NORMAL) this.applyViewMode(this.viewMode);
```

复制版本匹配的插件：

```bash
mkdir -p WebApps/ARServer/www/assets/krp/plugins
cp /home/wsy/workspace/Jingjie-AR/pano/html/assets/krp/1.20.7/plugins/webvr.js \
  WebApps/ARServer/www/assets/krp/plugins/webvr.js
```

- [ ] **Step 4: 运行适配层测试并确认 GREEN**

Run:

```bash
node --test tests/frontend/krpano-adapter.test.mjs
```

Expected: 全部 PASS，无 warning。

- [ ] **Step 5: 提交适配层任务**

```bash
git add WebApps/ARServer/www/js/krpano-adapter.js \
  WebApps/ARServer/www/assets/krp/plugins/webvr.js \
  tests/frontend/krpano-adapter.test.mjs
git commit -m "实现全景视角与VR控制"
```

---

### Task 2: 临时浮层状态机

**Files:**
- Create: `WebApps/ARServer/www/js/museum-ui-state.js`
- Create: `tests/frontend/museum-ui-state.test.mjs`

**Interfaces:**
- Produces: `MuseumUiState({ onChange?: (snapshot) => void })`
- Produces: `snapshot(): { sceneDrawerOpen: boolean, viewPanelOpen: boolean, viewMode: string }`
- Produces: `toggleSceneDrawer()`, `toggleViewPanel()`, `closeTransientLayers()`, `selectViewMode(mode)`
- Consumes: Task 1 的字符串模式值 `normal | planet | fisheye | crystal`，但不导入播放器模块。

- [ ] **Step 1: 写入状态机失败测试**

创建 `tests/frontend/museum-ui-state.test.mjs`：

```javascript
import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

async function loadModule() {
  const target = await mkdtemp(join(tmpdir(), "jingjie-ar-ui-state-"));
  const source = new URL("../../WebApps/ARServer/www/js/museum-ui-state.js", import.meta.url);
  const content = await readFile(source, "utf8");
  const modulePath = join(target, "museum-ui-state.mjs");
  await writeFile(modulePath, content);
  return { target, module: await import(pathToFileURL(modulePath).href) };
}

const loaded = await loadModule();
process.once("exit", () => rmSync(loaded.target, { recursive: true, force: true }));
const { MuseumUiState } = loaded.module;

test("场景抽屉和视角浮层始终互斥", () => {
  const state = new MuseumUiState();
  state.toggleSceneDrawer();
  assert.deepEqual(state.snapshot(), {
    sceneDrawerOpen: true, viewPanelOpen: false, viewMode: "normal"
  });
  state.toggleViewPanel();
  assert.deepEqual(state.snapshot(), {
    sceneDrawerOpen: false, viewPanelOpen: true, viewMode: "normal"
  });
});

test("关闭临时浮层不改变当前视角", () => {
  const state = new MuseumUiState();
  state.toggleViewPanel();
  state.selectViewMode("fisheye");
  state.toggleSceneDrawer();
  state.closeTransientLayers();
  assert.deepEqual(state.snapshot(), {
    sceneDrawerOpen: false, viewPanelOpen: false, viewMode: "fisheye"
  });
});

test("非法视角不会改变状态", () => {
  const state = new MuseumUiState();
  assert.throws(() => state.selectViewMode("flat"), /不支持的视角模式/);
  assert.equal(state.snapshot().viewMode, "normal");
});

test("仅在状态实际变化时通知快照", () => {
  const changes = [];
  const state = new MuseumUiState({ onChange: (snapshot) => changes.push(snapshot) });
  state.closeTransientLayers();
  assert.equal(changes.length, 0);
  state.toggleSceneDrawer();
  state.closeTransientLayers();
  assert.equal(changes.length, 2);
  assert.equal(changes[1].sceneDrawerOpen, false);
});
```

- [ ] **Step 2: 运行测试并确认 RED**

Run:

```bash
node --test tests/frontend/museum-ui-state.test.mjs
```

Expected: FAIL，明确指出 `museum-ui-state.js` 不存在。

- [ ] **Step 3: 实现纯状态机**

创建 `WebApps/ARServer/www/js/museum-ui-state.js`：

```javascript
const VIEW_MODES = new Set(["normal", "planet", "fisheye", "crystal"]);

export class MuseumUiState {
  constructor({ onChange } = {}) {
    this.sceneDrawerOpen = false;
    this.viewPanelOpen = false;
    this.viewMode = "normal";
    this.onChange = typeof onChange === "function" ? onChange : () => {};
  }

  snapshot() {
    return {
      sceneDrawerOpen: this.sceneDrawerOpen,
      viewPanelOpen: this.viewPanelOpen,
      viewMode: this.viewMode
    };
  }

  update(next) {
    const changed = Object.entries(next).some(([key, value]) => this[key] !== value);
    if (!changed) return this.snapshot();
    Object.assign(this, next);
    const snapshot = this.snapshot();
    this.onChange(snapshot);
    return snapshot;
  }

  toggleSceneDrawer() {
    return this.update({
      sceneDrawerOpen: !this.sceneDrawerOpen,
      viewPanelOpen: false
    });
  }

  toggleViewPanel() {
    return this.update({
      sceneDrawerOpen: false,
      viewPanelOpen: !this.viewPanelOpen
    });
  }

  closeTransientLayers() {
    return this.update({ sceneDrawerOpen: false, viewPanelOpen: false });
  }

  selectViewMode(mode) {
    if (!VIEW_MODES.has(mode)) throw new Error(`不支持的视角模式：${mode}`);
    return this.update({ viewMode: mode, viewPanelOpen: false });
  }
}
```

- [ ] **Step 4: 运行状态机测试并确认 GREEN**

Run:

```bash
node --test tests/frontend/museum-ui-state.test.mjs
```

Expected: 4 tests PASS。

- [ ] **Step 5: 提交状态机任务**

```bash
git add WebApps/ARServer/www/js/museum-ui-state.js \
  tests/frontend/museum-ui-state.test.mjs
git commit -m "实现展馆浮层状态管理"
```

---

### Task 3: 沉浸式页面语义结构与静态契约

**Files:**
- Modify: `tests/integration/museum_frontend_static_test.sh`
- Modify: `WebApps/ARServer/www/index.html`

**Interfaces:**
- Produces DOM IDs: `museum-shell`, `scene-drawer-toggle`, `scene-drawer`, `scene-catalog`, `fullscreen-toggle`, `music-toggle`, `vr-toggle`, `view-toggle`, `view-panel`
- Produces view buttons: `[data-view-mode="normal|planet|fisheye|crystal"]`
- Preserves IDs consumed by existing modules: `panorama`, `museum-title`, `museum-description`, `total-views`, `online-count`, all modal and form IDs.

- [ ] **Step 1: 扩展静态测试并形成 RED**

在 `tests/integration/museum_frontend_static_test.sh` 中加入：

```bash
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
grep -Fq 'aria-label="播放讲解"' "$index"
grep -Fq 'aria-label="进入 VR"' "$index"
grep -Fq 'aria-label="视角切换"' "$index"
grep -Fq '<svg' "$index"
! grep -Fq 'class="catalog-panel"' "$index"
! grep -Fq 'id="scene-title"' "$index"
! grep -Fq '360° PANORAMA' "$index"
! grep -Fq '>全屏浏览</button>' "$index"
! grep -Fq '>播放讲解</button>' "$index"
```

- [ ] **Step 2: 运行静态测试并确认 RED**

Run:

```bash
bash tests/integration/museum_frontend_static_test.sh
```

Expected: FAIL，首个新增失败为缺少 `id="museum-shell"`。

- [ ] **Step 3: 重组 `index.html` 主界面**

保留 `<head>`、三个业务弹窗、`notice`、`fatal-error` 和脚本标签，将 `.museum-shell` 到 `</main>` 的旧主界面替换为以下结构；每个 `.icon-button` 内放置对应的内嵌 SVG，SVG 使用 `viewBox="0 0 24 24"`、`aria-hidden="true"`、`fill="none"`、`stroke="currentColor"`：

```html
<main id="museum-shell" class="museum-shell">
  <section class="viewer-panel" aria-label="数字展馆全景浏览区">
    <div id="panorama" class="panorama" tabindex="0" aria-label="360 度全景展厅"></div>
    <div id="scene-loading" class="loading-pill" role="status">正在进入展厅…</div>

    <header class="floating-header" aria-label="展馆导航">
      <div class="glass-group identity">
        <span class="identity-accent" aria-hidden="true"></span>
        <h1 id="museum-title">正在载入展馆</h1>
      </div>
      <nav class="header-actions" aria-label="展馆操作">
        <button id="description-open" class="glass-button" type="button" aria-haspopup="dialog">展馆简介</button>
        <button id="login-open" class="glass-button" type="button" aria-haspopup="dialog">注册 / 登录</button>
      </nav>
      <div class="glass-group statistics" aria-label="展馆统计">
        <div><small>总浏览量</small><span id="total-views">—</span></div>
        <div><small>当前在线</small><span id="online-count">—</span></div>
      </div>
    </header>

    <nav class="viewer-toolbar" aria-label="全景工具">
      <button id="fullscreen-toggle" class="icon-button" type="button" title="全屏浏览" aria-label="全屏浏览"><svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor"><path d="M8 3H3v5M16 3h5v5M8 21H3v-5M16 21h5v-5"/></svg></button>
      <button id="music-toggle" class="icon-button" type="button" title="播放讲解" aria-label="播放讲解" disabled><svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor"><path d="M9 18V5l10-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="16" cy="16" r="3"/></svg></button>
      <button id="vr-toggle" class="icon-button" type="button" title="进入 VR" aria-label="进入 VR"><svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor"><path d="M3 8.5h18v8a2 2 0 0 1-2 2h-3.5l-2.1-3h-2.8l-2.1 3H5a2 2 0 0 1-2-2z"/><circle cx="7.5" cy="12.5" r="2"/><circle cx="16.5" cy="12.5" r="2"/></svg></button>
      <button id="view-toggle" class="icon-button" type="button" title="视角切换" aria-label="视角切换" aria-controls="view-panel" aria-expanded="false"><svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor"><path d="M2 12s3.5-6 10-6 10 6 10 6-3.5 6-10 6S2 12 2 12z"/><circle cx="12" cy="12" r="3"/></svg></button>
    </nav>

    <section id="view-panel" class="view-panel glass-group" aria-labelledby="view-panel-title" hidden>
      <h2 id="view-panel-title">视角切换</h2>
      <div class="view-options">
        <button type="button" data-view-mode="normal" aria-pressed="true"><svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor"><rect x="3" y="5" width="18" height="14" rx="2"/><path d="m6 16 4-4 3 3 2-2 3 3"/></svg><span>正常</span></button>
        <button type="button" data-view-mode="planet" aria-pressed="false"><svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor"><circle cx="12" cy="12" r="8"/><path d="M4.5 9.5c4 2 11 2 15 0M4.5 14.5c4-2 11-2 15 0M12 4c-3 4-3 12 0 16M12 4c3 4 3 12 0 16"/></svg><span>小行星</span></button>
        <button type="button" data-view-mode="fisheye" aria-pressed="false"><svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor"><circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="4"/><circle cx="12" cy="12" r="1"/></svg><span>鱼眼</span></button>
        <button type="button" data-view-mode="crystal" aria-pressed="false"><svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor"><circle cx="12" cy="10" r="7"/><path d="M7 19h10M9 17h6"/><path d="M7 8c2-3 7-4 10-1"/></svg><span>水晶球</span></button>
      </div>
    </section>

    <button id="scene-drawer-toggle" class="scene-drawer-toggle" type="button" aria-controls="scene-drawer" aria-expanded="false">
      <svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor"><rect x="4" y="4" width="6" height="6" rx="1"/><rect x="14" y="4" width="6" height="6" rx="1"/><rect x="4" y="14" width="6" height="6" rx="1"/><rect x="14" y="14" width="6" height="6" rx="1"/></svg><span>场景选择</span>
    </button>
    <section id="scene-drawer" class="scene-drawer" aria-label="场景选择" hidden>
      <nav id="scene-catalog" class="scene-catalog" aria-label="场景目录"></nav>
    </section>
    <audio id="scene-audio" preload="none"></audio>
  </section>
</main>
```

- [ ] **Step 4: 运行静态测试并确认 GREEN**

Run:

```bash
bash tests/integration/museum_frontend_static_test.sh
```

Expected: 输出 `PASS: museum frontend static shell`。

- [ ] **Step 5: 提交语义结构任务**

```bash
git add WebApps/ARServer/www/index.html \
  tests/integration/museum_frontend_static_test.sh
git commit -m "重构沉浸式展馆页面结构"
```

---

### Task 4: 高级玻璃界面与手机端响应式布局

**Files:**
- Modify: `tests/integration/museum_frontend_static_test.sh`
- Modify: `WebApps/ARServer/www/css/museum.css`

**Interfaces:**
- Consumes: Task 3 的 DOM 类名和 `hidden` 属性。
- Produces: `.is-open`、`.is-active`、`.is-playing`、`.is-fullscreen` 状态样式。

- [ ] **Step 1: 写入关键布局静态断言**

在静态测试中定义 `css="$root/css/museum.css"` 并加入：

```bash
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
grep -Fq '@media (prefers-reduced-motion: reduce)' "$css"
! grep -Fq 'grid-template-columns: minmax(0, 1fr) clamp(250px' "$css"
! grep -Fq '.catalog-panel' "$css"
```

- [ ] **Step 2: 运行静态测试并确认 RED**

Run:

```bash
bash tests/integration/museum_frontend_static_test.sh
```

Expected: FAIL，明确指出新沉浸式布局选择器或 `height: 100dvh` 缺失。

- [ ] **Step 3: 实现全屏和玻璃组件样式**

保留业务弹窗、表单、通知和致命错误样式；用以下确定规则替换旧 `.topbar`、`.museum-main`、`.viewer-heading`、`.viewer-tools`、`.catalog-panel` 和纵向 `.scene-catalog` 布局：

```css
:root {
  --glass: rgba(20, 19, 16, .52);
  --glass-strong: rgba(20, 19, 16, .76);
  --glass-line: rgba(255, 255, 255, .26);
  --accent: #d4ad65;
  --accent-strong: #f0ca7e;
}

html, body { width: 100%; height: 100%; overflow: hidden; }
.museum-shell { position: relative; width: 100%; height: 100vh; height: 100dvh; overflow: hidden; }
.viewer-panel, .panorama { position: absolute; inset: 0; }
.glass-group, .glass-button, .icon-button {
  border: 1px solid var(--glass-line);
  background: var(--glass);
  backdrop-filter: blur(14px);
  -webkit-backdrop-filter: blur(14px);
  box-shadow: 0 10px 34px rgba(0, 0, 0, .2);
}
.floating-header {
  position: absolute; z-index: 5; top: clamp(.75rem, 2vw, 1.5rem); left: clamp(.75rem, 2vw, 2rem); right: clamp(4.75rem, 7vw, 6rem);
  display: grid; grid-template-columns: minmax(0, auto) auto minmax(260px, auto); align-items: center; gap: .8rem;
}
.identity, .statistics, .glass-button { border-radius: 999px; }
.identity { min-width: 0; display: flex; align-items: center; gap: .75rem; padding: .72rem 1.1rem; }
.identity-accent { width: 2px; height: 1.5rem; background: var(--accent-strong); box-shadow: 0 0 12px rgba(240, 202, 126, .5); }
.identity h1 { margin: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-size: clamp(.9rem, 1.45vw, 1.22rem); }
.header-actions { display: flex; gap: .55rem; }
.glass-button { min-height: 44px; padding: .65rem 1rem; cursor: pointer; }
.statistics { justify-self: end; display: grid; grid-template-columns: 1fr 1fr; min-width: 260px; }
.statistics div { display: flex; align-items: baseline; justify-content: center; gap: .45rem; padding: .7rem 1rem; }
.statistics div + div { border-left: 1px solid var(--glass-line); }
.viewer-toolbar { position: absolute; z-index: 7; top: clamp(5.5rem, 12vh, 7rem); right: clamp(.75rem, 2vw, 2rem); display: grid; gap: .65rem; }
.icon-button { width: 46px; height: 46px; display: grid; place-items: center; padding: 0; border-radius: 50%; cursor: pointer; }
.icon-button svg { width: 23px; height: 23px; }
.icon-button:hover, .icon-button.is-active, .icon-button.is-playing { color: var(--accent-strong); border-color: var(--accent); background: var(--glass-strong); }
.view-panel { position: absolute; z-index: 6; top: clamp(16rem, 35vh, 19rem); right: clamp(4.4rem, 6vw, 5.8rem); width: min(430px, calc(100vw - 7rem)); padding: 1rem; border-radius: 18px; }
.view-panel[hidden], .scene-drawer[hidden] { display: none; }
.view-panel h2 { margin: 0 0 .8rem; text-align: center; font-size: 1rem; }
.view-options { display: grid; grid-template-columns: repeat(4, 1fr); gap: .35rem; }
.view-options button { min-width: 0; padding: .65rem .3rem; border: 0; border-radius: 12px; color: inherit; background: transparent; cursor: pointer; }
.view-options button[aria-pressed="true"] { color: var(--accent-strong); background: rgba(212, 173, 101, .13); }
.view-options svg { display: block; width: 30px; height: 30px; margin: 0 auto .35rem; }
.scene-drawer-toggle { position: absolute; z-index: 8; left: 1rem; bottom: 1rem; display: grid; justify-items: center; gap: .2rem; border: 0; color: #fff; background: transparent; cursor: pointer; text-shadow: 0 1px 6px #000; }
.scene-drawer-toggle svg { width: 42px; height: 42px; padding: 10px; border-radius: 50%; background: rgba(255, 255, 255, .86); color: #4a4032; }
.scene-drawer { position: absolute; z-index: 6; left: 0; right: 0; bottom: 0; padding: 1rem 5.5rem 1rem 7.5rem; background: rgba(19, 18, 15, .56); backdrop-filter: blur(12px); transform: translateY(105%); transition: transform .25s ease; }
.scene-drawer.is-open { transform: translateY(0); }
.scene-catalog { display: flex; gap: .75rem; overflow-x: auto; overscroll-behavior-inline: contain; scrollbar-width: thin; }
.scene-card { flex: 0 0 118px; display: grid; padding: 4px; border: 2px solid rgba(255, 255, 255, .8); background: rgba(10, 10, 9, .58); }
.scene-card img { width: 100%; height: 70px; object-fit: cover; }
.scene-card span { padding: .35rem .2rem .15rem; overflow: hidden; text-align: center; text-overflow: ellipsis; white-space: nowrap; }
.scene-card.is-current { border-color: var(--accent-strong); box-shadow: 0 0 0 1px rgba(240, 202, 126, .4); }
```

- [ ] **Step 4: 实现手机端两行布局和降低动态效果**

加入：

```css
@media (max-width: 820px) {
  .floating-header {
    top: .65rem; left: .65rem; right: 4.1rem;
    grid-template-areas: "identity statistics" "actions actions";
    grid-template-columns: minmax(0, 1fr) auto; gap: .45rem;
  }
  .identity { grid-area: identity; padding: .55rem .75rem; }
  .statistics { grid-area: statistics; min-width: 0; }
  .statistics div { display: grid; gap: .05rem; padding: .38rem .55rem; }
  .statistics small { font-size: .58rem; }
  .statistics span { font-size: .9rem; }
  .header-actions { grid-area: actions; }
  .glass-button { min-height: 40px; padding: .48rem .75rem; font-size: .82rem; }
  .viewer-toolbar { top: 8.4rem; right: .65rem; }
  .icon-button { width: 42px; height: 42px; }
  .view-panel { top: 12rem; right: 3.7rem; width: min(360px, calc(100vw - 4.35rem)); }
  .scene-drawer { padding: .75rem .75rem 4.8rem; }
  .scene-card { flex-basis: 104px; }
  .scene-card img { height: 62px; }
}

@media (prefers-reduced-motion: reduce) {
  *, *::before, *::after { scroll-behavior: auto !important; transition-duration: .01ms !important; animation-duration: .01ms !important; }
}
```

- [ ] **Step 5: 运行静态测试并提交**

Run:

```bash
bash tests/integration/museum_frontend_static_test.sh
```

Expected: PASS。

Commit:

```bash
git add WebApps/ARServer/www/css/museum.css \
  tests/integration/museum_frontend_static_test.sh
git commit -m "实现沉浸式展馆视觉布局"
```

---

### Task 5: 页面行为接线与原功能回归

**Files:**
- Modify: `tests/integration/museum_frontend_static_test.sh`
- Modify: `WebApps/ARServer/www/js/museum-app.js`

**Interfaces:**
- Consumes: `MuseumUiState` from Task 2。
- Consumes: `KrpanoAdapter#setViewMode`、`enterVr` from Task 1。
- Consumes: Task 3 DOM IDs and Task 4 state classes。

- [ ] **Step 1: 写入接线静态契约并确认 RED**

在静态测试中定义 `app="$root/js/museum-app.js"` 并加入：

```bash
grep -Fq 'import { MuseumUiState } from "./museum-ui-state.js"' "$app"
grep -Fq 'scene-drawer-toggle' "$app"
grep -Fq 'view-toggle' "$app"
grep -Fq 'data-view-mode' "$app"
grep -Fq 'fullscreenchange' "$app"
grep -Fq 'adapter.setViewMode' "$app"
grep -Fq 'adapter.enterVr' "$app"
grep -Fq 'pointerdown' "$app"
grep -Fq 'wheel' "$app"
grep -Fq 'event.key === "Escape"' "$app"
! grep -Fq 'element("scene-title")' "$app"
! grep -Fq 'element("scene-count")' "$app"
```

Run:

```bash
bash tests/integration/museum_frontend_static_test.sh
```

Expected: FAIL，明确指出缺少 `MuseumUiState` 导入。

- [ ] **Step 2: 接入 UI 状态渲染**

在 `museum-app.js` 增加导入：

```javascript
import { MuseumUiState } from "./museum-ui-state.js";
```

在 `app` 创建前创建状态，并通过以下函数同步 DOM：

```javascript
const uiState = new MuseumUiState({ onChange: renderUiState });

function renderUiState(state) {
  const drawer = element("scene-drawer");
  const drawerToggle = element("scene-drawer-toggle");
  drawer.hidden = !state.sceneDrawerOpen;
  drawer.classList.toggle("is-open", state.sceneDrawerOpen);
  drawerToggle.setAttribute("aria-expanded", String(state.sceneDrawerOpen));

  const viewPanel = element("view-panel");
  const viewToggle = element("view-toggle");
  viewPanel.hidden = !state.viewPanelOpen;
  viewToggle.classList.toggle("is-active", state.viewPanelOpen);
  viewToggle.setAttribute("aria-expanded", String(state.viewPanelOpen));

  document.querySelectorAll("[data-view-mode]").forEach((button) => {
    button.setAttribute("aria-pressed", String(button.dataset.viewMode === state.viewMode));
  });
}
```

删除 `scene-count` 和 `scene-title` 写入；场景卡片仍渲染到 `scene-catalog`。场景卡片点击处理改为：

```javascript
button.addEventListener("click", () => {
  uiState.closeTransientLayers();
  this.switchScene(scene.sceneId);
});
```

- [ ] **Step 3: 接入抽屉、外部关闭和视角切换**

在现有事件绑定区域加入：

```javascript
element("scene-drawer-toggle").addEventListener("click", (event) => {
  event.stopPropagation();
  uiState.toggleSceneDrawer();
});

element("view-toggle").addEventListener("click", (event) => {
  event.stopPropagation();
  uiState.toggleViewPanel();
});

element("scene-drawer").addEventListener("click", (event) => event.stopPropagation());
element("view-panel").addEventListener("click", (event) => event.stopPropagation());

document.querySelectorAll("[data-view-mode]").forEach((button) => {
  button.addEventListener("click", () => {
    const mode = button.dataset.viewMode;
    try {
      app.adapter.setViewMode(mode);
      uiState.selectViewMode(mode);
    } catch (error) {
      notify(error.message || "视角切换失败");
    }
  });
});

for (const type of ["pointerdown", "wheel"]) {
  element("panorama").addEventListener(type, () => uiState.closeTransientLayers(), { passive: true });
}
document.addEventListener("click", () => uiState.closeTransientLayers());
document.addEventListener("keydown", (event) => {
  if (event.key === "Escape") uiState.closeTransientLayers();
});
```

打开展馆简介、登录和作品弹窗前调用 `uiState.closeTransientLayers()`。

- [ ] **Step 4: 接入图标化音乐、全屏和 VR 状态**

将音乐状态的 `textContent` 写入改为以下辅助函数：

```javascript
function setMusicButtonState(state) {
  const button = element("music-toggle");
  const playing = state === "playing";
  button.classList.toggle("is-playing", playing);
  button.setAttribute("aria-label", playing ? "暂停讲解" : "播放讲解");
  button.title = button.disabled ? "当前场景暂无音乐" : button.getAttribute("aria-label");
}
```

`configureMusic()` 在无音乐时调用 `setMusicButtonState("unavailable")`，加载音乐后调用 `setMusicButtonState("paused")`，播放成功后调用 `setMusicButtonState("playing")`，暂停或结束后恢复 `paused`。

全屏目标改为 `museum-shell`，并增加状态同步：

```javascript
document.addEventListener("fullscreenchange", () => {
  const active = document.fullscreenElement === element("museum-shell");
  const button = element("fullscreen-toggle");
  button.classList.toggle("is-fullscreen", active);
  button.setAttribute("aria-label", active ? "退出全屏" : "全屏浏览");
  button.title = button.getAttribute("aria-label");
});
```

VR 点击处理：

```javascript
element("vr-toggle").addEventListener("click", () => {
  try {
    app.adapter.enterVr();
  } catch (error) {
    notify(error.message || "当前设备或浏览器无法进入 VR");
  }
});
```

- [ ] **Step 5: 运行前端与静态回归测试**

Run:

```bash
node --test tests/frontend/*.test.mjs
bash tests/integration/museum_frontend_static_test.sh
bash tests/integration/pano_migration_test.sh
```

Expected: 所有 Node 测试 PASS，两个 Bash 测试均输出 PASS。

- [ ] **Step 6: 提交页面行为任务**

```bash
git add WebApps/ARServer/www/js/museum-app.js \
  tests/integration/museum_frontend_static_test.sh
git commit -m "接入沉浸式展馆交互"
```

---

### Task 6: 完整构建与浏览器验收

**Files:**
- Modify only if a failing test exposes a defect: files owned by Tasks 1–5

**Interfaces:**
- Validates the complete branch against the approved design and existing server build.

- [ ] **Step 1: 运行完整前端与静态测试**

```bash
node --test tests/frontend/*.test.mjs
bash tests/integration/museum_frontend_static_test.sh
bash tests/integration/pano_migration_test.sh
bash tests/integration/assets_manifest_test.sh
```

Expected: 所有命令退出码为 0。

- [ ] **Step 2: 编译服务器目标**

```bash
cmake -S . -B build-full -DCMAKE_BUILD_TYPE=Debug
cmake --build build-full --target ar_server -j2
```

Expected: `ar_server` 构建成功，无编译错误。

- [ ] **Step 3: 使用浏览器人工验收清单**

启动测试环境后逐项验证：

1. 桌面端全景占满视口，固定右侧场景栏和当前场景标题不存在。
2. 顶部标题、展馆简介、登录入口和统计数据为三个玻璃分组。
3. 场景抽屉可打开；选择场景、拖动或缩放全景后自动关闭。
4. 场景抽屉和视角面板不会同时显示。
5. 全屏保留全部悬浮 UI；音乐图标正确表达不可用、暂停和播放状态。
6. 正常、小行星、鱼眼、水晶球效果可区分，场景切换后模式保持。
7. 支持设备进入 VR；不支持时出现中文局部提示。
8. 手机宽度下顶部变为两行，场景列表可横向滑动，控件无溢出。
9. 展馆简介、登录、热点、作品详情、点赞评论、总浏览量和当前在线人数正常。

- [ ] **Step 4: 提交验收阶段发现的测试驱动修复**

仅在 Step 1–3 发现并通过新增失败测试复现缺陷时创建提交：

```bash
git add WebApps/ARServer/www/index.html \
  WebApps/ARServer/www/css/museum.css \
  WebApps/ARServer/www/js/krpano-adapter.js \
  WebApps/ARServer/www/js/museum-ui-state.js \
  WebApps/ARServer/www/js/museum-app.js \
  tests/frontend/krpano-adapter.test.mjs \
  tests/frontend/museum-ui-state.test.mjs \
  tests/integration/museum_frontend_static_test.sh
git commit -m "修复沉浸式展馆验收问题"
```

Expected: 若没有发现缺陷，不创建空提交。
