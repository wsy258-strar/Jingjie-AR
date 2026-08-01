# Browser Host Timer Binding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复浏览器默认 `setInterval`/`clearInterval` 因接收者错误触发的 `Illegal invocation`，使访客心跳和展馆统计轮询可靠启动、停止。

**Architecture:** 在 `VisitorSession` 与 `MuseumLifecycle` 模块内部各保留私有宿主包装函数，默认依赖指向包装函数而非裸 `globalThis` 方法；显式注入函数不做重新绑定。业务生命周期、间隔数值和 bfcache generation 逻辑不变。

**Tech Stack:** 原生 ES Modules、Window timer APIs、Node.js 18 test runner。

## Global Constraints

- 只修改 `WebApps/ARServer/www/js/visitor-session.js`、`WebApps/ARServer/www/js/museum-lifecycle.js`、`tests/frontend/visitor-session.test.mjs`、`tests/frontend/museum-lifecycle.test.mjs`。
- 默认定时器必须以 `globalThis` 为合法接收者；显式注入的函数保持原有调用契约。
- 不修改 30 秒访客心跳、15 秒计数轮询、bootstrap 幂等、bfcache generation、退出在线状态或统计恢复语义。
- 不修改 C++ 后端、数据库、Redis、krpano 或已完成的 `fetch` 绑定。
- 严格先红后绿；使用 `/tmp/jingjie-final-node-runtime/nodejs/bin/node` 执行测试。

---

### Task 1: 修复访客心跳与统计轮询的宿主定时器绑定

**Files:**
- Modify: `WebApps/ARServer/www/js/visitor-session.js`
- Modify: `WebApps/ARServer/www/js/museum-lifecycle.js`
- Test: `tests/frontend/visitor-session.test.mjs`
- Test: `tests/frontend/museum-lifecycle.test.mjs`

**Interfaces:**
- Consumes: 两个构造函数现有的 `setIntervalImpl`、`clearIntervalImpl` 可选注入参数。
- Produces: 私有 `browserSetInterval(callback, delay)` 与 `browserClearInterval(timer)` 默认包装；公共类接口不变。

- [ ] **Step 1: 为 VisitorSession 写默认宿主 receiver 的失败测试**

在 `tests/frontend/visitor-session.test.mjs` 增加一个测试。先保存全局方法，再替换成必须以 `globalThis` 为 receiver 的宿主式函数；构造时不传定时器实现，验证创建和清除均经过合法 receiver，最后恢复全局状态：

```javascript
test("默认心跳定时器以 globalThis 作为宿主接收者", () => {
  const originalSetInterval = globalThis.setInterval;
  const originalClearInterval = globalThis.clearInterval;
  let callback;
  try {
    globalThis.setInterval = function (received, delay) {
      assert.equal(this, globalThis);
      assert.equal(delay, 30 * 1000);
      callback = received;
      return 41;
    };
    globalThis.clearInterval = function (timer) {
      assert.equal(this, globalThis);
      assert.equal(timer, 41);
    };
    const session = new VisitorSession({
      client: { request: async () => ({}) },
      storage: storageWith(),
      eventTarget: null
    });
    session.startHeartbeat();
    assert.equal(typeof callback, "function");
    session.stopHeartbeat({ sendExit: false });
    assert.equal(session.heartbeatTimer, null);
  } finally {
    globalThis.setInterval = originalSetInterval;
    globalThis.clearInterval = originalClearInterval;
  }
});
```

- [ ] **Step 2: 为 MuseumLifecycle 写默认宿主 receiver 的失败测试**

在 `tests/frontend/museum-lifecycle.test.mjs` 增加同类测试；使用最小 visitor 替身，验证 15 秒轮询创建和清除：

```javascript
test("默认统计定时器以 globalThis 作为宿主接收者", () => {
  const originalSetInterval = globalThis.setInterval;
  const originalClearInterval = globalThis.clearInterval;
  try {
    globalThis.setInterval = function (callback, delay) {
      assert.equal(this, globalThis);
      assert.equal(typeof callback, "function");
      assert.equal(delay, 15 * 1000);
      return 52;
    };
    globalThis.clearInterval = function (timer) {
      assert.equal(this, globalThis);
      assert.equal(timer, 52);
    };
    const lifecycle = new MuseumLifecycle({
      visitor: { bootstrap: async () => null },
      eventTarget: null
    });
    lifecycle.startCounterPolling();
    lifecycle.stopCounterPolling();
    assert.equal(lifecycle.counterTimer, null);
  } finally {
    globalThis.setInterval = originalSetInterval;
    globalThis.clearInterval = originalClearInterval;
  }
});
```

- [ ] **Step 3: 运行两个定向测试并确认按预期失败**

Run:

```bash
/tmp/jingjie-final-node-runtime/nodejs/bin/node --test \
  tests/frontend/visitor-session.test.mjs \
  tests/frontend/museum-lifecycle.test.mjs
```

Expected: 两个新增用例失败，receiver 实际为 `VisitorSession` 或 `MuseumLifecycle`，而非 `globalThis`；其余既有用例通过。

- [ ] **Step 4: 实现最小宿主包装函数**

在两个模块中分别于现有 `browserEventTarget()` 附近增加私有函数：

```javascript
function browserSetInterval(callback, delay) {
  return globalThis.setInterval(callback, delay);
}

function browserClearInterval(timer) {
  return globalThis.clearInterval(timer);
}
```

把两个构造函数的默认参数改为：

```javascript
setIntervalImpl = typeof globalThis.setInterval === "function" ? browserSetInterval : null,
clearIntervalImpl = typeof globalThis.clearInterval === "function" ? browserClearInterval : null
```

在 `VisitorSession.stopHeartbeat()` 中使用：

```javascript
if (typeof this.clearIntervalImpl === "function") {
  this.clearIntervalImpl(this.heartbeatTimer);
}
this.heartbeatTimer = null;
```

在 `MuseumLifecycle.stopCounterPolling()` 中使用：

```javascript
if (typeof this.clearIntervalImpl === "function") {
  this.clearIntervalImpl(this.counterTimer);
}
this.counterTimer = null;
```

- [ ] **Step 5: 运行定向测试并确认转绿**

Run:

```bash
/tmp/jingjie-final-node-runtime/nodejs/bin/node --test \
  tests/frontend/visitor-session.test.mjs \
  tests/frontend/museum-lifecycle.test.mjs
```

Expected: 两个文件全部测试通过，新增 receiver 用例通过。

- [ ] **Step 6: 运行完整前端和静态页面验证**

Run:

```bash
/tmp/jingjie-final-node-runtime/nodejs/bin/node --test tests/frontend/*.test.mjs
bash tests/integration/museum_frontend_static_test.sh
git diff --check
```

Expected: 7 个前端测试文件 0 失败；静态页面测试输出 `PASS: museum frontend static shell`；差异检查无输出。

- [ ] **Step 7: 提交任务**

```bash
git add WebApps/ARServer/www/js/visitor-session.js \
  WebApps/ARServer/www/js/museum-lifecycle.js \
  tests/frontend/visitor-session.test.mjs \
  tests/frontend/museum-lifecycle.test.mjs
git commit -m "修复浏览器定时器调用绑定"
```

Expected: 提交只包含上述 4 个文件，不包含 `.superpowers`、`build-*` 或本地 krpano 运行文件。
