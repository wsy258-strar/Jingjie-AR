# 浏览器宿主定时器绑定修复设计

## 背景与根因

展馆页面已经能够通过 `ApiClient` 拉取目录、统计和场景数据，并成功加载 krpano 全景；初始化末尾仍出现 `Failed to execute ... Illegal invocation`。调用链表明异常发生在 `MuseumApp.bootstrap()` 完成场景加载后启动访客心跳与页面统计轮询时。

`VisitorSession` 和 `MuseumLifecycle` 当前把 `globalThis.setInterval`、`globalThis.clearInterval` 作为默认参数保存到实例字段，之后通过 `this.setIntervalImpl(...)` 和 `this.clearIntervalImpl(...)` 调用。部分浏览器要求这些 Window 宿主方法以合法的全局对象作为接收者；实例方法形式会把业务对象作为 `this`，从而抛出 `Illegal invocation`。

## 目标

- 浏览器默认路径以合法的 `globalThis` 接收者调用 `setInterval` 和 `clearInterval`。
- 访客心跳与在线人数、总浏览量轮询均能正常启动和停止。
- 保留构造函数现有的定时器依赖注入能力，测试或调用方显式传入的函数不被重新绑定。
- 不改变访客 bootstrap、浏览量幂等、bfcache generation、退出在线状态及统计刷新语义。

## 非目标

- 不修改 C++ 后端、数据库、Redis 或 krpano。
- 不调整心跳和统计轮询间隔。
- 不重构页面初始化错误提示或其他浏览器宿主 API。
- 不改变 `ApiClient` 已完成的原生 `fetch` 绑定修复。

## 方案选择

采用浏览器包装函数作为默认依赖：包装函数内部通过成员访问表达式调用 `globalThis.setInterval(...)` 和 `globalThis.clearInterval(...)`。两个业务类仍通过注入字段调用包装函数，因此显式注入的测试实现保持原有 receiver 语义。

不直接在业务方法中硬编码 `window.setInterval`，以免破坏测试替身；也不在每个构造函数默认参数中重复 `.bind(globalThis)`，避免分散宿主绑定规则。

## 组件修改

### VisitorSession

- 默认 `setIntervalImpl` 和 `clearIntervalImpl` 改为宿主包装函数。
- `startHeartbeat()`、`stopHeartbeat()` 的去重、pagehide、pageshow 和异步恢复逻辑保持不变。

### MuseumLifecycle

- 使用同样语义的宿主包装函数作为统计轮询默认依赖。
- `startCounterPolling()`、`stopCounterPolling()` 以及 generation 检查保持不变。

包装函数可在各自模块内保持私有；本次不为了两行调用引入新的公共模块。

## 错误与兼容性

- 浏览器没有提供定时器函数时，现有 `typeof ... === "function"` 防护继续生效；停止路径不得在清理函数不存在时产生新的异常。
- 显式注入的函数由调用方负责其 receiver 需求，本修复不改变注入契约。
- 默认路径不依赖 `window` 名称，继续使用 `globalThis`，兼容浏览器测试环境。

## 测试设计

严格按 TDD 实施：

1. 在 `visitor-session.test.mjs` 中临时替换 `globalThis.setInterval/clearInterval` 为要求 `this === globalThis` 的宿主式函数，验证默认心跳创建与清除。旧实现必须以 receiver 断言失败。
2. 在 `museum-lifecycle.test.mjs` 中以相同方式验证默认统计轮询创建与清除。旧实现必须失败。
3. 保留并运行现有显式注入测试，证明注入函数未被强制绑定。
4. 使用 Node 18 运行全部 `tests/frontend/*.test.mjs`。
5. 运行 `museum_frontend_static_test.sh` 与 `git diff --check`。

## 验收标准

- 两个新增回归测试均先红后绿。
- 默认定时器的创建和清除调用 receiver 均为 `globalThis`。
- 现有前端测试全部通过。
- 页面硬刷新后不再出现由心跳或统计轮询触发的 `Illegal invocation`。
