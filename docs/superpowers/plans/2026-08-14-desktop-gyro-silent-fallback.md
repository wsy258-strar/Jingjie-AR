# PC 端陀螺仪静默降级 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** PC 浏览器缺少陀螺仪时不显示误导提示，同时保留移动端不可用以及权限拒绝、异常提示。

**Architecture:** `gyro-controller.js` 导出可独立测试的 `isMobileDevice()`，优先采用 User-Agent Client Hints，缺失时回退到受限移动 UA。`MuseumApp` 将判定结果作为 `notifyUnavailable` 注入 `GyroController`；该开关只控制插件 `unavailable` 提示，不影响权限拒绝或异常提示。

**Tech Stack:** 原生 ES Modules、Node.js `node:test`、Navigator User-Agent Client Hints。

## Global Constraints

- PC 的 Gyro2 `unavailable` 必须静默降级为拖动浏览。
- Android、iPhone、iPad、iPod 的 Gyro2 `unavailable` 仍最多提示一次。
- 权限明确拒绝或权限请求异常仍最多提示一次。
- 不使用视口宽度或触摸能力判定移动设备。
- 不修改数据库、浏览统计或作品互动接口。

---

### Task 1: 设备判定与提示策略

**Files:**
- Modify: `WebApps/ARServer/www/js/gyro-controller.js`
- Modify: `WebApps/ARServer/www/js/museum-app.js`
- Test: `tests/frontend/gyro-controller.test.mjs`
- Test: `tests/frontend/museum-app-wiring.test.mjs`

**Interfaces:**
- Produces: `isMobileDevice(navigatorObject): boolean`
- Produces: `new GyroController({ notifyUnavailable: boolean })`

- [x] **Step 1: 写失败测试**

在控制器测试中覆盖 Client Hints 优先级、受限 UA 回退、PC `unavailable` 静默、权限拒绝仍提示；在应用接线测试中断言桌面 navigator 注入 `false`。

- [x] **Step 2: 运行测试并验证 RED**

Run: `node --test tests/frontend/gyro-controller.test.mjs tests/frontend/museum-app-wiring.test.mjs`

Expected: 缺少 `isMobileDevice`、`notifyUnavailable` 和应用注入，测试失败。

- [x] **Step 3: 写最小实现**

```js
export function isMobileDevice(navigatorObject = globalThis.navigator) {
  if (typeof navigatorObject?.userAgentData?.mobile === "boolean")
    return navigatorObject.userAgentData.mobile;
  return /Android|iPhone|iPad|iPod/i.test(navigatorObject?.userAgent || "");
}
```

控制器构造器保存 `notifyUnavailable`；仅在插件 `unavailable` 分支按该值提示。`MuseumApp` 导入判定函数并传入 `isMobileDevice(window.navigator)`。

- [x] **Step 4: 运行完整回归**

Run:

```bash
node --test tests/frontend/*.test.mjs
bash tests/integration/museum_frontend_static_test.sh
git diff --check
```

Expected: 所有前端测试通过，静态测试输出 `PASS`，diff 检查无输出。

- [x] **Step 5: 提交并推送**

```bash
git add WebApps/ARServer/www/js/gyro-controller.js WebApps/ARServer/www/js/museum-app.js \
  tests/frontend/gyro-controller.test.mjs tests/frontend/museum-app-wiring.test.mjs
git add -f docs/superpowers/plans/2026-08-14-desktop-gyro-silent-fallback.md
git commit -m "重新启用 PC 端陀螺仪静默降级"
git push
```

## Plan Self-Review

- Spec coverage: 覆盖 PC 静默、移动端提示、权限失败提示与设备判定优先级。
- Placeholder scan: 无占位符。
- Type consistency: `isMobileDevice()` 与 `notifyUnavailable` 均使用布尔语义。
- Scope: 不涉及数据库、总浏览量或作品互动代码。
