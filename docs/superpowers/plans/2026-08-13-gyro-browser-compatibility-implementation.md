# 移动端陀螺仪浏览器兼容修复 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 Gyro2 1.20.7 以插件生命周期事件驱动启用，在 Android 类浏览器自动工作，在 iOS 类浏览器于首次真实手势中完成方向与运动权限申请，并在不可用时安全降级。

**Architecture:** `KrpanoAdapter` 负责把 Gyro2 的四个生命周期事件从 krpano XML 桥接为稳定的 JavaScript 回调，并只通过 `plugin[gyro].enabled` 控制插件。`GyroController` 维护“插件可用、浏览器权限、暂停原因、销毁状态”四组正交状态；`MuseumApp` 只转发生命周期事件和用户手势，不再用场景图片加载事件猜测插件是否就绪。

**Tech Stack:** 原生 ES Modules、Node.js 20.19.5 `node:test`、krpano 1.20.7 Gyro2、Pointer Events、Nginx `Permissions-Policy`。

## Global Constraints

- 不新增页面按钮、弹窗或操作入口；首次 `pointerdown` 继续作为显式权限入口。
- Android Chrome/Edge/Samsung Internet 一类没有 `requestPermission()` 的环境，仅在收到 Gyro2 `available` 后自动启用。
- 若 `DeviceOrientationEvent` 与 `DeviceMotionEvent` 都提供 `requestPermission()`，必须在同一用户手势内同时申请且两者均为 `granted` 才可启用。
- 权限申请不得以前置 `plugin[gyro].isavailable` 为条件；权限与插件就绪允许任意先后顺序。
- Gyro2 1.20.7 只通过 `set(plugin[gyro].enabled,true|false);` 启停。
- 权限拒绝、权限异常、传感器缺失、插件不可用、非安全上下文或 Permissions Policy 阻止时最多提示一次，并保留手指拖动。
- `suspend`、`resume`、`destroy` 及 modal/transient-ui 生命周期语义保持不变。
- 前端测试使用 Node.js 20；系统 `/usr/bin/node` v12 不得用于判断结果。

---

### Task 1: 桥接 Gyro2 生命周期并改用公开启停接口

**Files:**
- Modify: `WebApps/ARServer/www/js/krpano-adapter.js:31-59,169-171,191-266,308-324`
- Modify: `tests/frontend/krpano-adapter.test.mjs:135-168,230-265`

**Interfaces:**
- Consumes: krpano 调用的全局函数 `JingjieARGyroBridge(eventCode)`，其中 `0/1/2/3` 分别表示 `unavailable/available/enabled/disabled`。
- Produces: `new KrpanoAdapter({ onGyroStateChange(state) })`；`state` 为上述四个字符串之一。
- Produces: `enableGyro(): boolean` 与 `disableGyro(): boolean`，分别执行 `set(plugin[gyro].enabled,true);` 和 `set(plugin[gyro].enabled,false);`。

- [ ] **Step 1: 写入生命周期和公开 API 的失败测试**

在 `tests/frontend/krpano-adapter.test.mjs` 中将现有 Gyro2 测试改为：

```js
test("场景 XML 注册 Gyro2 生命周期回调且默认由页面控制启用", () => {
  const xml = buildSceneXml(scene);
  assert.match(xml, /<plugin name="gyro" devices="html5" keep="true"/);
  assert.match(xml, /url="\/assets\/krp\/plugins\/gyro2\.js"/);
  assert.match(xml, /enabled="false"/);
  assert.match(xml, /onavailable="js\(JingjieARGyroBridge\(1\)\);"/);
  assert.match(xml, /onunavailable="js\(JingjieARGyroBridge\(0\)\);"/);
  assert.match(xml, /onenable="js\(JingjieARGyroBridge\(2\)\);"/);
  assert.match(xml, /ondisable="js\(JingjieARGyroBridge\(3\)\);"/);
});

test("Gyro2 适配器通过 enabled 属性启停并转发生命周期", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  const states = [];
  const player = {
    get(key) {
      assert.equal(key, "plugin[gyro].isavailable");
      return true;
    },
    call(command) { calls.push(command); }
  };
  globalThis.embedpano = (options) => options.onready(player);
  try {
    const adapter = new KrpanoAdapter({
      targetId: "panorama",
      onGyroStateChange: (state) => states.push(state)
    });
    await adapter.initialize();
    adapter.enableGyro();
    adapter.disableGyro();
    globalThis.JingjieARGyroBridge(1);
    globalThis.JingjieARGyroBridge(2);
    globalThis.JingjieARGyroBridge(3);
    globalThis.JingjieARGyroBridge(0);
    assert.deepEqual(calls, [
      "set(plugin[gyro].enabled,true);",
      "set(plugin[gyro].enabled,false);"
    ]);
    assert.deepEqual(states, ["available", "enabled", "disabled", "unavailable"]);
    assert.equal(adapter.isGyroAvailable(), true);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});
```

- [ ] **Step 2: 运行测试并确认按预期失败**

Run:

```bash
/tmp/node-v20.19.5-linux-x64/bin/node --test tests/frontend/krpano-adapter.test.mjs
```

Expected: FAIL，缺少四个 XML 生命周期属性、`JingjieARGyroBridge`，且实际命令仍为 `gyro.enable();` / `gyro.disable();`。

- [ ] **Step 3: 实现最小生命周期桥与启停命令**

在适配器模块级增加：

```js
let gyroBridge = null;

const GYRO_EVENTS = Object.freeze({
  0: "unavailable",
  1: "available",
  2: "enabled",
  3: "disabled"
});

globalThis.JingjieARGyroBridge = function (eventCode) {
  const event = GYRO_EVENTS[Number(eventCode)];
  if (event && gyroBridge) gyroBridge(event);
};
```

给 Gyro2 XML 节点加入四个属性：

```js
' onavailable="js(JingjieARGyroBridge(1));"',
' onunavailable="js(JingjieARGyroBridge(0));"',
' onenable="js(JingjieARGyroBridge(2));"',
' ondisable="js(JingjieARGyroBridge(3));"',
```

构造参数和初始化桥接使用：

```js
onGyroStateChange = () => {},
// constructor body
this.onGyroStateChange = typeof onGyroStateChange === "function"
  ? onGyroStateChange : () => {};
// onready body
gyroBridge = (event) => this.onGyroStateChange(event);
```

替换启停命令：

```js
this.player.call("set(plugin[gyro].enabled,true);");
this.player.call("set(plugin[gyro].enabled,false);");
```

- [ ] **Step 4: 运行适配器测试并确认通过**

Run: `/tmp/node-v20.19.5-linux-x64/bin/node --test tests/frontend/krpano-adapter.test.mjs`

Expected: PASS，适配器测试 0 失败。

- [ ] **Step 5: 提交独立适配器改动**

```bash
git add WebApps/ARServer/www/js/krpano-adapter.js tests/frontend/krpano-adapter.test.mjs
git commit -m "修复 Gyro2 生命周期桥接与启停接口"
```

### Task 2: 建立权限与插件就绪解耦的 GyroController 状态机

**Files:**
- Modify: `WebApps/ARServer/www/js/gyro-controller.js:1-73`
- Modify: `tests/frontend/gyro-controller.test.mjs:24-70`

**Interfaces:**
- Consumes: `deviceOrientation` 与 `deviceMotion`，二者可为空或提供 `requestPermission(): Promise<string>`。
- Consumes: `handlePluginState(state)`，`state` 为 `available | unavailable | enabled | disabled`。
- Produces: `requestFromGesture(): Promise<boolean>`、`autoEnable(): Promise<boolean>`、`enableIfAllowed(): boolean`、`suspend(reason): void`、`resume(reason): boolean`、`destroy(): void`。

- [ ] **Step 1: 写权限顺序、双权限和降级分支的失败测试**

在 `tests/frontend/gyro-controller.test.mjs` 中增加用例，使用记录 `enableGyro` / `disableGyro` 次数的真实小型适配器替身，逐项断言：

```js
test("Android 类环境等待 available 后自动启用", async () => {
  let enabled = 0;
  const controller = new GyroController({
    adapter: { enableGyro: () => { enabled += 1; }, disableGyro() {} },
    deviceOrientation: undefined,
    deviceMotion: undefined
  });
  assert.equal(await controller.autoEnable(), false);
  assert.equal(enabled, 0);
  assert.equal(controller.handlePluginState("available"), true);
  assert.equal(enabled, 1);
});

test("权限可在插件 available 前申请并于稍后启用", async () => {
  let requests = 0;
  let enabled = 0;
  const controller = new GyroController({
    adapter: { enableGyro: () => { enabled += 1; }, disableGyro() {} },
    deviceOrientation: { requestPermission: async () => { requests += 1; return "granted"; } },
    deviceMotion: undefined
  });
  assert.equal(await controller.requestFromGesture(), false);
  assert.equal(requests, 1);
  assert.equal(enabled, 0);
  assert.equal(controller.handlePluginState("available"), true);
  assert.equal(enabled, 1);
});

test("方向与运动权限在同一手势内均通过后才启用", async () => {
  const requests = [];
  let enabled = 0;
  const controller = new GyroController({
    adapter: { enableGyro: () => { enabled += 1; }, disableGyro() {} },
    deviceOrientation: { requestPermission: async () => { requests.push("orientation"); return "granted"; } },
    deviceMotion: { requestPermission: async () => { requests.push("motion"); return "granted"; } }
  });
  controller.handlePluginState("available");
  assert.equal(await controller.requestFromGesture(), true);
  assert.deepEqual(requests.sort(), ["motion", "orientation"]);
  assert.equal(enabled, 1);
});
```

再增加三个独立用例：任一权限返回 `denied`、任一权限抛异常、`unavailable` 后 suspend/resume 不会重启；每个分支断言 `onDenied` 总计只调用一次且没有未处理拒绝。

- [ ] **Step 2: 运行测试并确认旧逻辑失败**

Run: `/tmp/node-v20.19.5-linux-x64/bin/node --test tests/frontend/gyro-controller.test.mjs`

Expected: FAIL，旧控制器会在插件未 available 时跳过权限申请，不支持 `deviceMotion` / `handlePluginState`，且 Android 无法等待生命周期事件。

- [ ] **Step 3: 实现正交状态与单次提示**

控制器必须保存下列状态：

```js
this.deviceMotion = deviceMotion;
this.permissionRequired = [deviceOrientation, deviceMotion].some(
  (source) => typeof source?.requestPermission === "function"
);
this.permissionRequested = false;
this.permissionGranted = !this.permissionRequired;
this.pluginAvailable = false;
this.denialNotified = false;
this.permissionRequest = null;
```

`requestFromGesture()` 从两个事件构造器收集所有 `requestPermission`，用 `Promise.all` 在当前手势调用栈中启动请求；只有所有结果均为 `granted` 才设置 `permissionGranted=true`。`handlePluginState("available")` 设置 `pluginAvailable=true` 并调用 `enableIfAllowed()`；`unavailable` 设置为 false、关闭已启用插件并调用幂等 `notifyDeniedOnce()`；`enabled/disabled` 只同步 `gyroEnabled`。`enableIfAllowed()` 仅在未销毁、权限通过、插件 available、无暂停原因时写 enabled 属性。

- [ ] **Step 4: 运行控制器测试并确认通过**

Run: `/tmp/node-v20.19.5-linux-x64/bin/node --test tests/frontend/gyro-controller.test.mjs`

Expected: PASS，控制器测试 0 失败。

- [ ] **Step 5: 提交独立状态机改动**

```bash
git add WebApps/ARServer/www/js/gyro-controller.js tests/frontend/gyro-controller.test.mjs
git commit -m "修复陀螺仪权限与插件就绪竞态"
```

### Task 3: 让 MuseumApp 只转发生命周期与真实手势

**Files:**
- Modify: `WebApps/ARServer/www/js/museum-app.js:137-208,247-256,516-524`
- Modify: `tests/frontend/museum-app-wiring.test.mjs:426-490,684-731,830-896`

**Interfaces:**
- Consumes: `KrpanoAdapter({ onGyroStateChange })`。
- Consumes: `GyroController.handlePluginState(state)` 与 `requestFromGesture()`。
- Produces: 场景图片事件只控制 dissolve；Gyro2 生命周期独立控制陀螺仪。

- [ ] **Step 1: 写事件接线和去竞态失败测试**

扩展 `KrpanoAdapter` / `GyroController` 测试桩，使控制器记录 `pluginStates`；将旧的“预览可见后重试”两个用例替换为：

```js
test("Gyro2 available 独立于场景图片事件驱动自动启用", async () => {
  const harness = await createHarness();
  try {
    await harness.app.switchScene("scene-a");
    assert.equal(harness.app.gyro.autoEnableCalls, 0);
    harness.adapter.options.onSceneEvent({ type: "preview-visible", generation: 1 });
    assert.equal(harness.app.gyro.autoEnableCalls, 0);
    harness.adapter.options.onGyroStateChange("available");
    assert.deepEqual(harness.app.gyro.pluginStates, ["available"]);
  } finally {
    await harness.cleanup();
  }
});

test("插件未 available 时首次 panorama pointerdown 仍转发权限申请", async () => {
  const harness = await createHarness();
  try {
    const panorama = harness.document.getElementById("panorama");
    await panorama.dispatch("pointerdown");
    assert.equal(harness.app.gyro.requestFromGestureCalls, 1);
    harness.adapter.options.onGyroStateChange("available");
    assert.deepEqual(harness.app.gyro.pluginStates, ["available"]);
  } finally {
    await harness.cleanup();
  }
});
```

保留并调整 transient-ui、modal、pagehide 用例，确保 pause/resume/destroy 次数不回归。

- [ ] **Step 2: 运行接线测试并确认按预期失败**

Run: `/tmp/node-v20.19.5-linux-x64/bin/node --test tests/frontend/museum-app-wiring.test.mjs`

Expected: FAIL，适配器未接收 `onGyroStateChange`，应用仍从 scene preview/complete 和 generation 状态触发 `autoEnable()`。

- [ ] **Step 3: 删除图片加载触发并接入插件事件**

从 `MuseumApp` 删除 `gyroAutoEnabledGeneration`、`gyroAutoEnablingGeneration`、`gyroGestureRequested` 与 `autoEnableGyroAfterSceneLoad()`；删除 `onSceneEvent` 两个分支中的自动启用调用。适配器配置加入：

```js
onGyroStateChange: (state) => this.gyro?.handlePluginState(state),
```

`requestGyroFromGesture()` 直接捕获控制器错误并返回布尔值，不在应用层缓存成功状态；单次权限请求由控制器自身保证。

- [ ] **Step 4: 运行接线与生命周期回归测试**

Run:

```bash
/tmp/node-v20.19.5-linux-x64/bin/node --test \
  tests/frontend/gyro-controller.test.mjs \
  tests/frontend/krpano-adapter.test.mjs \
  tests/frontend/museum-app-wiring.test.mjs \
  tests/frontend/museum-lifecycle.test.mjs
```

Expected: PASS，4 个测试文件 0 失败。

- [ ] **Step 5: 提交应用接线改动**

```bash
git add WebApps/ARServer/www/js/museum-app.js tests/frontend/museum-app-wiring.test.mjs
git commit -m "改为 Gyro2 生命周期驱动应用接线"
```

### Task 4: 补齐部署策略、静态契约和全量验收

**Files:**
- Modify: `docs/operations/krpano-museum-deployment.md:203-286,288-310`
- Modify: `tests/integration/museum_frontend_static_test.sh`

**Interfaces:**
- Produces: Nginx 响应头 `Permissions-Policy: accelerometer=(self), gyroscope=(self)`。
- Produces: 部署后响应头检查命令和 Android/iOS 真机检查清单。

- [ ] **Step 1: 写部署文档静态契约失败测试**

在 `tests/integration/museum_frontend_static_test.sh` 末尾增加：

```bash
deployment_doc="docs/operations/krpano-museum-deployment.md"
grep -Fq 'Permissions-Policy "accelerometer=(self), gyroscope=(self)" always;' "$deployment_doc"
grep -Fq "permissions-policy" "$deployment_doc"
grep -Fq "Android Chrome" "$deployment_doc"
grep -Fq "iOS Safari" "$deployment_doc"
```

- [ ] **Step 2: 运行静态测试并确认失败**

Run: `bash tests/integration/museum_frontend_static_test.sh`

Expected: FAIL，部署文档尚无 Permissions-Policy 配置和真机检查项。

- [ ] **Step 3: 更新 Nginx 与验收文档**

在 `jingjie-ar-csp.conf` 相邻片段加入：

```nginx
add_header Permissions-Policy "accelerometer=(self), gyroscope=(self)" always;
```

将响应头命令扩展为：

```bash
curl -fsSI https://jingjiear.cn/index.html \
  | grep -Ei 'cache-control|content-security-policy|permissions-policy'
```

真机清单明确 Android Chrome 自动启用、关闭“动作传感器”后的拖动降级、iOS Safari 首触授权、拒绝后的单次提示，以及作品弹窗打开暂停/关闭恢复。

- [ ] **Step 4: 执行完整自动回归**

Run:

```bash
/tmp/node-v20.19.5-linux-x64/bin/node --test tests/frontend/*.test.mjs
bash tests/integration/museum_frontend_static_test.sh
git diff --check
```

Expected: 所有前端测试 PASS，静态集成测试退出码 0，`git diff --check` 无输出。

- [ ] **Step 5: 提交部署与验收文档**

```bash
git add docs/operations/krpano-museum-deployment.md tests/integration/museum_frontend_static_test.sh
git commit -m "补充陀螺仪权限策略与真机验收"
```

## Plan Self-Review

- Spec coverage: 四个任务分别覆盖 XML 四事件、官方启停 API、Android 延迟 available、iOS 权限先行、双权限拒绝/异常、暂停恢复与单次提示、Nginx Permissions Policy 和真机验收。
- Placeholder scan: 无未定义占位步骤；每个代码改动均有精确文件、失败原因、实现接口和验证命令。
- Type consistency: 生命周期状态统一为 `available | unavailable | enabled | disabled`；适配器回调和控制器入口均使用 `onGyroStateChange(state)` / `handlePluginState(state)`。
