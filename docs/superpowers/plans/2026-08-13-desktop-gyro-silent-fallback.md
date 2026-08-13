# PC 端陀螺仪静默降级 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** PC 浏览器缺少陀螺仪时不显示误导提示，同时保留移动端不可用和权限失败提示。

**Architecture:** `gyro-controller.js` 提供可独立测试的 `isMobileDevice()`，优先采用 User-Agent Client Hints，回退到受限移动 UA。`MuseumApp` 将判定结果作为 `notifyUnavailable` 注入 `GyroController`；控制器只抑制插件 `unavailable` 提示，不抑制权限拒绝或异常提示。

**Tech Stack:** 原生 ES Modules、Node.js 20.19.5 `node:test`、Navigator User-Agent Client Hints。

## Global Constraints

- PC 的 Gyro2 `unavailable` 必须静默降级。
- Android、iPhone、iPad、iPod 的 Gyro2 `unavailable` 仍最多提示一次。
- 权限明确拒绝或权限请求异常在所有需要权限的环境中仍最多提示一次。
- 不使用视口宽度或触摸能力作为移动设备判定依据。

---

### Task 1: 设备判定与提示策略

**Files:**
- Modify: `WebApps/ARServer/www/js/gyro-controller.js`
- Modify: `WebApps/ARServer/www/js/museum-app.js`
- Modify: `tests/frontend/gyro-controller.test.mjs`
- Modify: `tests/frontend/museum-app-wiring.test.mjs`

**Interfaces:**
- Produces: `isMobileDevice(navigatorObject): boolean`。
- Consumes: `new GyroController({ notifyUnavailable: boolean })`。

- [x] **Step 1: 写失败测试**

```js
assert.equal(isMobileDevice({ userAgentData: { mobile: false }, userAgent: "Android" }), false);
assert.equal(isMobileDevice({ userAgentData: { mobile: true }, userAgent: "Desktop" }), true);
assert.equal(isMobileDevice({ userAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64)" }), false);
assert.equal(isMobileDevice({ userAgent: "Mozilla/5.0 (Linux; Android 14)" }), true);
```

再构造 `notifyUnavailable: false` 的控制器，调用 `handlePluginState("unavailable")` 后断言 `onDenied` 为 0；构造需要权限且拒绝的同一控制器，断言仍提示 1 次。MuseumApp 接线测试断言桌面 navigator 注入 `false`。

- [x] **Step 2: 验证 RED**

Run: `/tmp/node-v20.19.5-linux-x64/bin/node --test tests/frontend/gyro-controller.test.mjs tests/frontend/museum-app-wiring.test.mjs`

Expected: FAIL，缺少 `isMobileDevice`、`notifyUnavailable` 和应用注入。

- [x] **Step 3: 最小实现**

```js
export function isMobileDevice(navigatorObject = globalThis.navigator) {
  if (typeof navigatorObject?.userAgentData?.mobile === "boolean")
    return navigatorObject.userAgentData.mobile;
  return /Android|iPhone|iPad|iPod/i.test(navigatorObject?.userAgent || "");
}
```

构造器保存 `this.notifyUnavailable = Boolean(notifyUnavailable)`；仅在处理插件 `unavailable` 时按该值决定是否调用 `notifyDeniedOnce()`。`MuseumApp` 导入该函数并传入 `notifyUnavailable: isMobileDevice(window.navigator)`。

- [x] **Step 4: 验证 GREEN 与回归**

Run:

```bash
/tmp/node-v20.19.5-linux-x64/bin/node --test tests/frontend/*.test.mjs
bash tests/integration/museum_frontend_static_test.sh
git diff --check
```

Expected: 16 个前端测试文件通过，静态测试 PASS，diff 检查无输出。

- [x] **Step 5: 提交**

```bash
git add WebApps/ARServer/www/js/gyro-controller.js WebApps/ARServer/www/js/museum-app.js \
  tests/frontend/gyro-controller.test.mjs tests/frontend/museum-app-wiring.test.mjs
git add -f docs/superpowers/plans/2026-08-13-desktop-gyro-silent-fallback.md
git commit -m "修复 PC 端陀螺仪不可用误提示"
```

## Plan Self-Review

- Spec coverage: 覆盖 PC 静默、移动端提示、权限失败提示和 UA Client Hints 优先级。
- Placeholder scan: 无占位符。
- Type consistency: `notifyUnavailable` 始终为布尔值；`isMobileDevice()` 始终返回布尔值。
