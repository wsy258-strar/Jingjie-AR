# HTTP Framework + ARServer：13 项任务实施记录

> 记录日期：2026-07-23  
> 对应设计：[正式设计文档](superpowers/specs/2026-07-21-http-framework-ar-server-design.md)；[实施计划](superpowers/plans/2026-07-21-http-framework-ar-server.md)。  
> 本文记录实现内容、关键函数、功能边界、问题处理与验证证据。配置文件、数据库密码及会话令牌不写入仓库。

## 总览

最终交付包含两个目标：

- `http_framework`：可安装、可由外部 C++11 程序链接的 HTTP 框架。
- `ar_server`：位于 `WebApps/ARServer` 的 AR 场景会话应用。

原有 Multi-Reactor、`TcpServer`、`Buffer`、`TimerQueue`、日志、内存池、MySQL、Redis、`DBWorkerPool`、`SessionDAO`、`SessionCache` 与 `TwoLevelCache` 均被保留。数据库和 Redis 操作均通过工作线程执行，不在 EventLoop 中等待。

## 任务 1：HTTP 模型与解析边界

**完成内容**

- 完善 `HttpRequest`、`HttpResponse` 和 `HttpContext`，形成增量 HTTP/1.1 解析链路。
- 支持请求行、请求头、`Content-Length` body、Query 参数、Cookie、路径参数和请求属性。
- 增加请求行、头、body 的大小上限及明确的 400/413/501 错误分类。

| 关键函数 | 功能 |
|---|---|
| `HttpContext::parseRequest()` | 在 `Buffer` 上增量解析；数据不足时等待下一次读取，不把半包当错误。 |
| `processRequestLine()` | 提取方法、路径、Query 与 HTTP 版本；拒绝未知方法或格式错误请求行。 |
| `parseHeaders()` / `parseBody()` | 校验头格式、限制累计头大小和 body 大小，处理 `Content-Length`。 |
| `HttpContext::reset()` | Keep-Alive 请求之间清空解析状态。 |
| `HttpResponse::appendToBuffer()` | 按 HTTP/1.1 格式组装状态行、头和 body。 |

**异常与处理**

- `Transfer-Encoding` 既往处理不完整，可能造成歧义；现统一拒绝不支持的传输编码并返回 501。
- 超长请求行、头或 body 以前可能只落到通用 400；现映射为 413，便于客户端修正请求。

**验证**：`http_protocol_test` 覆盖分段到达、连续请求、非法 `Content-Length`、超限和传输编码错误。

## 任务 2：静态/动态 Router

**完成内容**

- 提供按 HTTP 方法注册的精确路由与 `:name` 动态路径路由。
- 路由匹配后将动态参数写入请求副本，避免污染后续请求。
- 重复 `(method, pattern)` 注册返回失败；方法不匹配时返回 405 和 `Allow`。

| 关键函数 | 功能 |
|---|---|
| `Router::add()` / `addAsync()` | 注册同步或异步路由。 |
| `Router::compileDynamicPattern()` | 将 `/api/scenes/:sceneId` 编译为受限正则并保留参数名。 |
| `Router::route()` | 依次匹配精确路由、动态路由，返回 handled/pending/not-found/method-not-allowed。 |
| `Router::injectParameters()` | 将捕获到的路径段写入 `HttpRequest` 路径参数表。 |
| `Router::invoke()` | 对同步回调写 `HttpResponse`；对异步回调交付 `AsyncResponder`。 |

**异常与处理**

- 参考实现中动态路由参数可能因复用请求对象而丢失；改为 `HttpRequest routedRequest(request)` 后再交给 handler，参数只属于本次路由。
- 重复路由容易因“后注册覆盖前注册”而产生不确定行为；用 `registeredPatterns_` 显式拒绝。

**验证**：`router_test` 覆盖精确优先、动态参数、405/Allow、重复注册和异步路由。

## 任务 3：Middleware 链与 CORS

**完成内容**

- 建立 before/after 中间件链，after 按逆序运行。
- 增加标准 CORS 配置与预检处理；增加 Request ID 和访问日志中间件。

| 关键函数 | 功能 |
|---|---|
| `MiddlewareChain::processBefore()` | 顺序运行 before；短路时记录已执行中间件。 |
| `MiddlewareChain::processAfter()` | 倒序运行 after；保留第一个异常并确保其余 after 仍有机会收尾。 |
| `CorsMiddleware::before()` | 处理 OPTIONS 预检、Origin/方法/请求头的许可判定。 |
| `CorsMiddleware::after()` | 为普通响应补充允许的 CORS 头。 |
| `CorsMiddleware::addCorsHeaders()` | 根据实际 Origin 设置响应并维护正确的 `Vary: Origin`。 |
| `AccessLogMiddleware::sanitizeTarget()` | 将 URL 中 `password` 参数替换为 `[REDACTED]` 后再记录。 |

**异常与处理**

- 原参考代码会无条件反射 Origin 或遗漏 `Vary`，会导致代理缓存串用跨域响应；现仅允许配置 Origin，通配策略与凭据策略分开处理。
- 中间件抛异常或短路时 after 可能漏执行；现保证已执行的中间件逆序收尾，并给出安全 500 响应。

**验证**：`middleware_test`、`request_middleware_test` 覆盖短路、异常、CORS 预检、`Vary`、password 脱敏。

## 任务 4：Dispatcher 与同步 HttpServer API

**完成内容**

- `HttpDispatcher` 连接 Router、Middleware、同步 fallback 与异步 fallback。
- `HttpServer` 对外提供 `Get/Post/Put/Delete/Options` 和对应 async 注册 API。
- 解析循环支持同一读取缓冲区中的多个完整同步请求。

| 关键函数 | 功能 |
|---|---|
| `HttpDispatcher::dispatch()` | 执行 before、路由、fallback、after，并返回是否存在异步未完成请求。 |
| `HttpDispatcher::completeAsync()` | 在异步响应完成时补跑本次请求的 after 链。 |
| `HttpServer::onMessage()` | 循环解析并分发同步 pipeline 请求。 |
| `HttpServer::onRequest()` | 依据 HTTP 版本和 `Connection` 头决定关闭策略并创建响应。 |
| `HttpServer::sendParseError()` | 将解析错误转化为统一 HTTP 错误响应。 |

**异常与处理**

- 请求要求关闭时仍继续解析后续 pipeline，可能在已关闭连接上继续分发；`shouldContinueParsing()` 现在在关闭响应或异步 pending 时停止。
- 路由/中间件异常以前可能穿透网络线程；`dispatch()` 捕获异常并返回 JSON 500。

**验证**：`http_dispatcher_test`、`http_server_test` 覆盖同步分发、关闭响应与 pipeline 停止条件。

## 任务 5：通用 Session Core

**完成内容**

- 提供不依赖 AR 业务的 `Session` 值对象、内存存储、编解码接口与 `SessionManager`。
- 支持 TTL、刷新、销毁、Cookie/Authorization token 提取及可插拔校验器。

| 关键函数 | 功能 |
|---|---|
| `Session::setValue()` / `value()` / `expired()` | 管理 Session 键值和过期判断。 |
| `MemorySessionStorage::save/load/remove()` | 线程安全内存存储，读取时淘汰过期项。 |
| `SessionManager::create/save/load/refresh/destroy()` | Session 生命周期门面。 |
| `SessionManager::secureId()` | 生成不可预测的 session id。 |
| `SessionManager::extractToken()` | 从 Authorization Bearer 或 Cookie 中提取 token。 |

**异常与处理**

- 以前 Session 存储线程安全边界不清晰；内存存储在互斥保护下处理读写和过期淘汰。
- token 不能使用可预测计数器；使用系统安全随机源生成标识。

**验证**：`session_manager_test`、`session_codec_test` 覆盖 TTL、刷新、删除、token 提取和二进制编码。

## 任务 6：Exactly-once 异步响应与安全 DB 回调

**完成内容**

- 引入 `AsyncResponder`，确保每个异步 HTTP 请求最多响应一次。
- DBWorkerPool 接受任务后保证只执行一次；连接借用失败也以空 `shared_ptr<MYSQL>` 回调。
- DAO 回调改为 `shared_ptr` 或值语义，避免异步裸指针悬垂。

| 关键函数 | 功能 |
|---|---|
| `AsyncResponder::send()` | 使用原子 completed 标志，首次发送成功，重复发送返回 false。 |
| `HttpServer::asyncResponderFactory()` | 以弱连接引用和 EventLoop 回投发送异步响应，断连则安全丢弃。 |
| `DBWorkerPool::submit()` / `run()` | 有界提交并在关闭时清理接受过的任务。 |
| `SessionDAO::findUserByUsername()` 等 | 在 DB 工作线程使用预处理语句，将结果包装为 `shared_ptr<User/Session>`。 |

**异常与处理**

- `runSync()` 会阻塞 EventLoop，且超时后 promise 生命周期不安全；业务路径改为回调式异步，测试场景保留受控用途。
- 客户端在 DB 回调前断开连接可能触发 use-after-free；发送端只持有 `weak_ptr<TcpConnection>`，连接不存在即返回。
- 回调捕获短生命周期裸指针风险已消除；DAO 结果使用共享所有权或值传递。

**验证**：`async_responder_test`、`ar_handlers_test` 与 `disconnected_client_test.sh`。

## 任务 7：有界缓存工作线程与 Redis Session

**完成内容**

- 新增有界 `TaskWorkerPool`，将同步 hiredis 操作移出 EventLoop。
- 新增 `RedisSessionStorage`，使用 `http_session:{token}` 命名空间存取通用 Session。

| 关键函数 | 功能 |
|---|---|
| `TaskWorkerPool::submit()` | 队列未满且未停止时接收任务；否则立即返回 false，形成背压。 |
| `TaskWorkerPool::run()` | 工作线程消费队列，析构时有序停止。 |
| `RedisSessionStorage::encode/decode()` | 二进制安全地序列化/恢复 Session。 |
| `RedisSessionStorage::save/load/remove()` | 通过 SETEX/GET/DEL 操作 Redis；连接或解析失败返回 false。 |

**异常与处理**

- hiredis 是同步 API，直接在 EventLoop 调用会卡住网络线程；所有调用均从 `TaskWorkerPool` 发起。
- 无界缓存任务会在 Redis 异常时耗尽内存；队列容量固定，超载调用方得到明确失败。

**验证**：`task_worker_pool_test`、`session_codec_test`、`redis_session_integration`（Redis 可用时）。

## 任务 8：可复用静态资源与安全 sendfile

**完成内容**

- 将静态文件能力提取为 `StaticFileHandler`，支持条件请求、Range、大文件异步读取和 AR MIME 类型。
- 修复 `sendFile()` 的部分发送、EPOLLOUT 和 fd 所有权释放。

| 关键函数 | 功能 |
|---|---|
| `StaticFileHandler::prepareInRoot()` | 规范化路径并阻止目录穿越。 |
| `prepare()` / `populateResponse()` | 计算 ETag、mtime、Range、缓存命中并构造 200/206/304 响应。 |
| `handleAsync()` | 将 stat/读取等阻塞工作交给缓存工作线程，再异步响应。 |
| `StaticFileHandler::mime()` | 补充 `.patt`、`.glb`、`.gltf`、`.bin` 等 AR 资源类型。 |
| `TcpConnection::sendFile/sendPendingFile/closePendingFile()` | 管理 partial send、EPOLLOUT 续传及关闭时 fd 回收。 |

**异常与处理**

- 原 `sendFile()` 没有完整处理部分发送，可能截断文件或泄漏 fd；增加 pending file 状态并在写事件和连接销毁时关闭 fd。
- 静态读取放在 I/O 线程会造成卡顿；文件准备和大文件读取交给有界工作池。

**验证**：`static_file_handler_test`、`static_file_server_test.sh`，以及页面源文件逐字节校验。

## 任务 9：迁移应用至 WebApps/ARServer

**完成内容**

- 将 AR 应用固定在 `WebApps/ARServer`，前端 `index.html`、`style.css`、`app.js` 仅移动，内容保持字节一致。
- 划分 framework、Redis 扩展和 AR 数据层目标；ARServer 只通过框架 API 注册 handler。
- 实现认证、会话读取、进入/退出场景、MySQL 回源和 Redis 缓存更新。

| 关键函数 | 功能 |
|---|---|
| `ARServer::ARServer()` | 注册 Middleware、认证/会话/Presence 路由和静态 async fallback。 |
| `AuthService::authenticate()` | 新用户创建、已有用户哈希核验、创建业务会话。 |
| `AuthService::passwordHash()` / `token()` | SHA-256 带标识哈希；从安全随机源生成会话 token。 |
| `SessionService::get/enter/exit()` | 编排业务会话查询、入场和离场。 |
| `DaoSessionStore` / `CachedSessionStore` | 分别适配 MySQL DAO 与 Redis+MySQL 两级回源。 |
| `SessionDAO::updateSessionScene/endSession()` | 使用单条预处理 SQL 原子更新场景和状态。 |
| `ArSessionValidator::validate()` | 异步检查缓存、必要时回源 MySQL 并回填缓存。 |

**异常与处理**

- ARServer 不再直接包含/依赖 `Channel`、`Socket` 或 `TcpConnection`，隔离网络细节。
- Redis 不可用时，读取从 MySQL 回源；关键写入失败返回 503，不伪造成功。
- 数据库 token 查询需能显示已退出会话，DAO 查询改为获取最新记录；校验器再依据业务要求判定 active 状态。

**验证**：`ar_handlers_test`、`ar_api_test.sh`、`redis_fallback_test.sh` 与前端哈希校验。

## 任务 10：Presence、场景元数据与弱协作 API

**完成内容**

- 增加固定场景列表、心跳、成员轮询和空交互接口。
- Presence 存储在 Redis `scene:{sceneId}:presence`，用时间戳判断在线并在 TTL 后自动离线。

| 关键函数 | 功能 |
|---|---|
| `PresenceService::heartbeat/remove/list()` | 提供业务级心跳、移除和在线成员查询。 |
| `RedisPresenceStore::touch/active/remove()` | 通过 Redis 记录 token 时间戳、删除离场 token、过滤过期成员。 |
| `PresenceHandlers::heartbeat()` | 校验 token/scene 后异步写入 Presence。 |
| `PresenceHandlers::members()` | 动态读取 `:sceneId` 参数并返回成员列表。 |
| `SceneHandlers::list/get/interactions()` | 返回场景元数据；交互接口明确返回尚未实现。 |

**异常与处理**

- 不引入 WebSocket，采用 HTTP 心跳和轮询，避免超出本阶段范围。
- 成员过期受调度粒度影响，测试增加合理调度裕量，避免把正常定时延迟误判为业务失败。

**验证**：`presence_service_test`、`presence_handlers_test`、Redis Presence 集成测试及 `ar_collaboration_test.sh`。

## 任务 11：请求中间件、环境配置与错误契约

**完成内容**

- 添加 `AppConfig`，从环境读取端口、MySQL、Redis、线程数和静态目录。
- 清除源码中的数据库密码；`.env.arserver` 保持本地未跟踪。
- 定义 API 的 400/401/404/405/409/501/503 JSON 失败响应。

| 关键函数 | 功能 |
|---|---|
| `AppConfig::fromEnvironment()` | 收集白名单环境变量，避免隐式读取任意环境。 |
| `AppConfig::fromMap()` | 校验数值范围和 MySQL 密码必填条件，生成可读错误。 |
| `AuthMiddleware::before()` | 对受保护 `/api/` 路径抽取 token，缺失时返回 401 JSON。 |
| `AuthHandler::validate/handle()` | 校验认证请求，并将服务层结果映射为 200/401/503。 |
| `ApiError` / `JsonUtil::escape()` | 生成安全、可解析的 JSON 错误和字符串。 |

**异常与处理**

- 明文数据库密码曾位于 `src/main.cpp`；现只允许 `MYSQL_PASSWORD` 环境输入，启动时缺失会退出并给出配置错误。
- 密码仍因旧前端兼容性出现在 query string；访问日志在任何情况下脱敏该字段。

**验证**：`app_config_test`、`request_middleware_test`、手工启动检查和 API 失败路径测试。

## 任务 12：可安装 CMake 包与外部消费者证明

**完成内容**

- 导出 `http_framework` CMake target、头文件和 `http_frameworkConfig.cmake`。
- 允许关闭 MySQL/Redis 构建 framework core；可选导出 `http_framework_redis` 与 `ar_data`。
- 新增外部消费者工程，证明 `find_package()`、链接、路由注册和 Middleware 注册可独立完成。

| 配置项/目标 | 功能 |
|---|---|
| `install(TARGETS http_framework ...)` | 安装静态/共享目标及导出文件。 |
| `configure_package_config_file()` | 生成下游 `find_package(http_framework CONFIG REQUIRED)` 配置。 |
| `HTTP_FRAMEWORK_WITH_MYSQL/REDIS` | 允许框架按依赖可用性进行 core-only 构建。 |
| `tests/consumer/CMakeLists.txt` | 作为第三方项目链接已安装框架。 |
| `consumer_build_test.sh` | 安装、配置、构建、运行消费者的端到端证明。 |

**异常与处理**

- 初始 include/链接依赖传播过宽，可能让下游强制依赖数据库；改为按 target 作用域声明依赖。
- 消费者脚本权限不足会导致 CI 不能执行；已设置为可执行文件。

**验证**：`tests/integration/consumer_build_test.sh` 成功构建并运行外部消费者。

## 任务 13：端到端、故障演练、Sanitizer 与性能证据

**完成内容**

- 补齐静态文件、AR API、两人协作、断连客户端、Redis 回源、Presence TTL 的脚本。
- 提供 ASan/UBSan、关键 TSan 和 Release 压测脚本；性能矩阵记录于 `benchmark/benchmark.md`。
- README 写入启动、验收、性能复现与故障演练步骤。

| 验证入口 | 覆盖功能 |
|---|---|
| `ar_api_test.sh` | 认证、进入/退出、会话、心跳、场景 API 与错误状态。 |
| `ar_collaboration_test.sh` | 两个用户同场可见、退出和过期离线。 |
| `disconnected_client_test.sh` | 异步 DB 响应前客户端断开，不发生 use-after-free 或重复发送。 |
| `redis_fallback_test.sh` | Redis 失效后 Session 回源 MySQL。 |
| `static_file_server_test.sh` | 首页和 CSS 与源文件逐字节一致。 |
| `benchmark/` 脚本 | 长/短连接并发、延迟分位和过载错误数。 |

**异常与处理**

- 测试机器缺少 `jq` 时集成脚本不可用；以 Python 标准库 JSON 辅助脚本替换外部依赖。
- TSan 地址空间布局在某些 Linux 环境冲突；记录并使用 `setarch -R` workaround，仍以关键用例验证竞态。
- Redis 集成测试在受限 sandbox 中可能报 `Can't create socket: Operation not permitted`；在允许本机 socket 的环境重跑通过，问题不属于业务断言失败。

**验证结论**：常规 CTest 19/19 通过；外部消费者、静态文件、API、协作、断连和 Redis 回源脚本通过；ASan/UBSan 与关键 TSan 用例通过。

## 后续运行故障记录：Keep-Alive 异步认证错误（已修复）

在用户实际浏览器验证中发现一个未被单请求 `curl` 覆盖的问题：先加载首页后，浏览器复用同一 HTTP/1.1 Keep-Alive 连接调用 `POST /api/auth`，服务错误返回 `index.html`。前端对 HTML 调用 `response.json()` 抛出异常，界面因此显示“服务器未启动或 MySQL 未连接”。

**定位方法**：使用同一条 curl 连接依次发送 `GET /` 和 `POST /api/auth`；第二个响应确认为 `Content-Type: text/html` 和首页 body，而单独 POST 正常返回认证 JSON。

**根因**：静态首页走异步 fallback；`HttpServer::onMessage()` 在 `kAsyncPending` 返回时没有 `HttpContext::reset()`。下一次读取保留了前一条 GET 的解析状态，因此把 POST 再次按 GET 分发。

**修复**：在 `HttpServer::onMessage()` 中，异步 handler 已复制请求后调用 `context.reset()` 再返回。新增 `tests/integration/keep_alive_async_route_test.sh`，固定验证“首页 → 同连接认证”必须得到 JSON。修复提交：`87bb1c4`；README 记录提交：`e50270b`。

## 交接提示

- 使用 `set -a; source .env.arserver; set +a` 后再启动 `./build-full/bin/ar_server`；不要提交 `.env.arserver`。
- 前端必须从 `http://127.0.0.1:8080/` 打开，不应直接在浏览器地址栏访问 `/api/auth`。
- 认证 token 是凭证，不应复制到日志、Issue 或提交信息。
- 新增异步 handler 时必须使用 `AsyncResponder`，不得在 EventLoop 中调用 MySQL、Redis、`future::get()` 或 `future::wait_for()`。
