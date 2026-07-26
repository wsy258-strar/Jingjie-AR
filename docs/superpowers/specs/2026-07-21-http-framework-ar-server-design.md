# 可嵌入式 C++ HTTP 框架与 AR 场景应用设计

日期：2026-07-21

## 1. 背景与目标

当前项目已经具备自研 Multi-Reactor 网络栈、HTTP/1.1 增量解析、异步日志、定时器、内存池、MySQL 连接池、DBWorkerPool、Redis 缓存和 AR 场景会话接口。现状的主要问题是网络框架、HTTP 应用层和 AR 业务代码集中在同一可执行程序中，业务开发者仍需了解底层连接对象，路由、中间件和通用 Session 等框架能力也缺少稳定接口。

项目将参考并选择性移植 `Kama-HTTPServer` 的应用层实现，在保留现有自研网络栈和数据基础设施的前提下，形成两个独立交付物：

1. `http_framework`：可被其他 C++ 程序链接的 HTTP 服务框架。
2. `ar_server`：使用该框架开发的 AR 场景应用。

首版 AR 应用定位为多人弱协作系统。用户可以注册或登录、进入不同场景、维持在线心跳、查询同场景成员并退出场景。前端场景交互保留接口但不实现，当前 HTML、CSS 和 JavaScript 文件内容在框架重构期间保持不变，之后再替换为 AR.js 场景。

## 2. 已确认的设计决策

- 目标岗位方向为 C++ 后端与基础架构，项目作为简历主项目，计划用 2 至 3 周完成。
- 保留现有 Reactor、TcpServer、Buffer、TimerQueue、日志、内存池、MySQL、Redis、DBWorkerPool、SessionDAO、SessionCache 和 TwoLevelCache。
- 已获得复用 `Kama-HTTPServer` 所需代码的授权。
- 只选择性移植路由、中间件、Session、CORS 等应用层代码，不引入 Muduo 网络栈。
- 项目继续使用 C++11；移植代码不得依赖 C++17 结构化绑定、`std::any` 或 `std::make_unique`。
- 示例应用目录命名为 `WebApps`，不使用 `examples`。
- 当前 `www/index.html`、`www/css/style.css` 和 `www/js/app.js` 的文件内容在首轮重构中保持不变。
- 多人状态使用 HTTP 心跳和轮询，不在首版引入 WebSocket。
- HTTPS 作为 AR.js 正式部署阶段的可选扩展，不阻塞首版框架重构。

## 3. 范围

### 3.1 首版范围

- HTTP/1.1 增量解析、Keep-Alive 和请求体大小限制。
- GET、POST、PUT、DELETE 和 OPTIONS 方法。
- 静态路由、具名动态路径参数、查询参数和 404/405 分流。
- 全局中间件、请求短路和逆序响应后处理。
- CORS、访问日志、请求 ID 和认证中间件。
- 通用 SessionManager、SessionStorage、MemorySessionStorage 和 RedisSessionStorage。
- AR 用户认证、进入场景、会话查询、心跳、成员轮询和退出场景。
- MySQL 最终持久化、Redis 热数据缓存和 Redis 在线成员集合。
- 静态文件服务、安全路径映射、小文件缓存和大文件发送。
- 单元测试、集成测试、故障测试、并发检查和性能回归。
- 可链接框架库、ARServer 可执行程序及 CMake 接入示例。

### 3.2 首版不包含

- WebSocket 和实时姿态同步。
- 场景交互数据模型及交互逻辑。
- HTTP/2、HTTP/3 和跨平台网络后端。
- 模板引擎、ORM 和插件热加载。
- AR.js 页面本身的实现。
- 生产环境 TLS 终止；正式 AR.js 部署可在下一阶段移植 Kama 的 OpenSSL 层或在反向代理处终止 TLS。

## 4. 集成策略

采用“选择性移植并适配”，不采用原样合并或整体替换。

| 能力 | 来源 | 处理方式 |
|---|---|---|
| Reactor、Socket、Channel、Buffer、Timer | 当前项目 | 原样保留并作为框架底座 |
| HttpRequest、HttpResponse、HttpContext | 当前项目 | 保留，补充 OPTIONS、路径参数、Cookie 和边界限制 |
| Router、RouterHandler | Kama | 移植接口和主要实现，替换 Muduo 类型并修复动态参数问题 |
| Middleware、MiddlewareChain | Kama | 移植结构，改为显式短路协议并删除未使用的链式指针 |
| CorsConfig、CorsMiddleware | Kama | 选择性移植，按每个请求的 Origin 正确生成响应 |
| Session、SessionManager、SessionStorage | Kama | 移植接口，替换随机数方案并补充线程安全和 Redis 实现 |
| SSL | Kama | 首版不移植，保留清晰扩展点 |
| MySQL、Redis 和两级缓存 | 当前项目 | 保留并通过应用服务层组合 |
| 五子棋 WebApp | Kama | 不移植 |

移植时必须修正以下已识别问题：

- Kama 的动态回调匹配后调用了原请求，导致提取出的路径参数丢失。
- Kama 用 `param1`、`param2` 暴露参数，需改为模式中的真实参数名。
- Kama 用抛出 `HttpResponse` 的方式中断 CORS 预检，需改为显式返回短路结果。
- Kama 的 MemorySessionStorage 未加锁，不能直接用于多 Reactor 线程。
- Kama 的 CORS 后处理在白名单模式下固定返回第一个 Origin，需根据当前请求 Origin 判断。
- Kama 的 HTTP Server 直接依赖 Muduo、C++17 和 OpenSSL，不能进入当前框架核心。

## 5. 目标目录与构建产物

目标目录如下：

```text
include/
├── base/
├── net/
├── timer/
├── log/
├── http/
├── router/
├── middleware/
│   └── cors/
├── session/
├── db/
└── cache/

src/
├── base/
├── net/
├── timer/
├── log/
├── http/
├── router/
├── middleware/
├── session/
├── db/
└── cache/

WebApps/
└── ARServer/
    ├── include/
    │   ├── handlers/
    │   ├── services/
    │   └── repositories/
    ├── src/
    │   ├── handlers/
    │   ├── services/
    │   ├── repositories/
    │   └── main.cpp
    └── www/
        ├── index.html
        ├── css/style.css
        └── js/app.js

tests/
├── unit/
└── integration/
```

构建目标：

- `http_framework`：包含 base、net、timer、log、http、router、middleware 和通用 session。
- `http_framework_redis`：可选适配库，包含 RedisSessionStorage 并链接现有 RedisConnectionPool；未安装 hiredis 时不构建。
- `ar_server`：链接 `http_framework`、可选的 `http_framework_redis`，并组合 db、cache 和 AR 业务模块。
- `http_framework_tests`：框架单元测试。
- `ar_server_tests`：AR API 与数据层集成测试。

框架同时支持构建静态库和共享库。公共头文件只暴露 `include/` 下的稳定接口，`WebApps/ARServer` 不进入框架库。

`http_framework` 本身可以在没有 MySQL 和 Redis 的环境中独立构建。`ar_server` 也保留现有的可选依赖编译模式，但没有 hiredis 时心跳和成员接口固定返回 503；多人弱协作的完整验收构建必须同时启用 MySQL 和 hiredis。

## 6. 框架公共 API

ARServer 使用如下接口注册路由和中间件：

```cpp
HttpServer server(&loop, addr, "ARServer");

server.Get("/", indexHandler);
server.Get("/api/session", getSessionHandler);
server.Get("/api/scenes/:sceneId/members", membersHandler);
server.Post("/api/auth", authHandler);
server.Post("/api/session/enter", enterSceneHandler);
server.Post("/api/session/heartbeat", heartbeatHandler);
server.Post("/api/session/exit", exitSceneHandler);

server.addMiddleware(std::make_shared<CorsMiddleware>(corsConfig));
server.addMiddleware(std::make_shared<AccessLogMiddleware>());
server.addMiddleware(std::make_shared<AuthMiddleware>(publicPaths));

server.setThreadNum(3);
server.start();
loop.loop();
```

路由注册必须在 `start()` 前完成。服务器启动后路由表和中间件表只读，工作线程可无锁并发查询。

同步路由使用现有 `HttpCallback`。需要访问 MySQL 或 Redis 的路由使用异步注册接口：

```cpp
using AsyncHandler = std::function<void(
    const HttpRequest&,
    const AsyncResponder&)>;

server.PostAsync("/api/auth", authHandler);
```

AsyncResponder 是可复制、线程安全且只能成功响应一次的句柄。Handler 可以将它复制到工作线程任务中；调用 `send(response)` 时，框架自动把发送动作投递回连接所属 EventLoop。同步和异步 Handler 在 Router 内部使用不同的明确类型，不通过运行时猜测 Handler 是否完成。

## 7. 请求处理生命周期

```text
TcpConnection 收到字节
  -> HttpContext 增量解析
  -> 生成 HttpRequest
  -> MiddlewareChain::processBefore
  -> Router 匹配并执行 Handler
  -> MiddlewareChain::processAfter（逆序）
  -> HttpResponse 序列化
  -> TcpConnection 发送
```

`onMessage` 必须循环处理缓冲区内所有完整请求，以支持同一次读取包含多个 HTTP pipelined 请求的情况。解析器对未完成请求保留状态，对格式错误、过大请求头或过大请求体生成明确错误并关闭连接。

默认限制如下：

- 请求行最大 8 KiB。
- 请求头总大小最大 32 KiB。
- 请求体最大 1 MiB，可在服务器启动前配置。
- 不支持的 Transfer-Encoding 明确返回 501，不按 Content-Length 误解析。

## 8. 路由设计

Router 使用“HTTP 方法 + 路径”作为匹配键：

- 静态路由用 `unordered_map` 精确匹配。
- 动态路由保存编译后的正则表达式和参数名列表。
- `/api/scenes/:sceneId/members` 匹配后通过 `request.pathParameter("sceneId")` 获取值。
- 路径不存在返回 404；路径存在但方法不匹配返回 405，并携带 `Allow` 响应头。
- 重复注册相同方法和路径在启动阶段报错，避免静默覆盖。

RouterHandler 对象形式用于有依赖和多步骤逻辑的业务处理器，回调形式用于简单路由。两者最终进入同一 Handler 调用抽象。

## 9. 中间件设计

中间件接口为：

```cpp
class Middleware {
public:
    virtual ~Middleware() {}
    virtual bool before(HttpRequest& request, HttpResponse& response) = 0;
    virtual void after(const HttpRequest& request, HttpResponse& response) = 0;
};
```

`before()` 返回 `true` 时继续执行；返回 `false` 时表示响应已生成，后续中间件和路由不再执行。已经成功执行 `before()` 的中间件仍按相反顺序执行 `after()`。

默认执行顺序：

```text
CORS.before
  -> AccessLog.before
  -> Auth.before
  -> Router
  -> Auth.after
  -> AccessLog.after
  -> CORS.after
```

- CORS 预检生成 204 并短路。
- CORS 在 `allowCredentials=true` 时不得返回通配 Origin，而是校验并回显当前请求 Origin，同时添加 `Vary: Origin`。
- Auth 对公开路径放行，对受保护路径解析 token、拒绝缺失 token，并把 token 写入请求属性；需要 Redis/MySQL 的真实性验证由异步业务服务完成，避免中间件阻塞 EventLoop。
- AccessLog 记录 request_id、方法、路径、状态码、耗时和缓存命中情况；认证 URL 的密码参数必须脱敏。
- 未捕获的业务异常在框架边界转换为 500，异常不得越过 EventLoop 回调边界。

## 10. Session 分层

框架 Session 与 AR 业务 Session 使用同一 token，但职责和存储命名空间分离。

### 10.1 框架 Session

SessionManager 负责 token 创建、Cookie/Authorization/查询参数提取、TTL 续期和销毁。SessionStorage 提供 `save`、`load` 和 `remove` 接口。

- MemorySessionStorage 用于测试和单进程示例，内部 map 受互斥锁保护。
- RedisSessionStorage 用于多实例部署，键名为 `http_session:{token}`。
- Session ID 使用操作系统安全随机源生成，不使用 `mt19937` 作为安全令牌生成器。
- Cookie 支持 HttpOnly、SameSite 和 Secure 配置。

当前页面继续从查询参数传递 token，因此无需修改文件内容。AuthMiddleware 同时支持查询参数、Cookie 和 Authorization Bearer token；后续 AR.js 页面优先使用 Cookie 或 Authorization。

SessionManager 负责令牌的通用生命周期，但不独占 AR 身份校验。受保护路由的业务服务接收可注入的 SessionValidator：普通框架应用可以使用 SessionStorage，ARServer 使用 ArSessionValidator。ArSessionValidator 先读取 `http_session:{token}` 和现有 SessionCache；Redis 不可用或未命中时调用 SessionDAO 回源 MySQL，成功后重新写入两个 Redis 命名空间。这样通用 Session 缓存故障不会阻断已经确认的 MySQL 降级路径。

### 10.2 AR 业务 Session

AR Session 包含用户 ID、场景 ID、进入状态和时间字段：

- MySQL `users` 是用户注册信息的最终数据源。
- MySQL `sessions` 是登录记录和进入/退出状态的最终数据源。
- 现有 SessionDAO 和 DBWorkerPool 保留。
- 现有 SessionCache 保留，Redis 键名继续使用 `session:{token}`。
- Redis 未命中或不可用时回源 MySQL，并在成功读取后回填 Redis。
- 写操作先提交 MySQL，再更新或失效 Redis 缓存。

框架 Session 不包含 `scene_id` 等领域字段；ARServer 通过服务层组合 SessionManager、SessionDAO 和 SessionCache，避免业务字段进入框架核心。

## 11. 多人弱协作与在线状态

首版使用 Redis 有序集合维护在线成员：

- 键：`scene:{sceneId}:presence`。
- 成员：session token。
- score：最近心跳的 Unix 毫秒时间戳。
- 心跳间隔：10 秒。
- 在线超时：30 秒。
- 成员轮询间隔：3 至 5 秒。

数据流：

```text
注册/登录
  -> MySQL 查询或创建用户
  -> SessionManager 创建 token
  -> MySQL 创建 session 行
  -> Redis 写入框架 Session 和 AR Session 缓存

进入场景
  -> AuthMiddleware 提取 token
  -> ArSessionValidator 异步验证 token
  -> MySQL 更新 scene_id/status
  -> 更新 SessionCache
  -> Redis ZADD 在线集合

心跳
  -> Redis ZADD 更新时间
  -> 续期两个 Session 命名空间的 TTL
  -> 不在每次心跳写 MySQL

查询成员
  -> Redis 删除超过 30 秒的成员
  -> 查询有效 token
  -> 从 SessionCache 批量获取可公开成员信息

退出场景
  -> MySQL 更新 status=0 并清空 scene_id
  -> Redis ZREM 在线集合
  -> 更新或失效 SessionCache
```

Redis 在线集合是“当前在线”的权威来源；MySQL `status` 表示最后一次持久化的进入/退出状态。进程重启后，缺少有效 Redis 心跳的 MySQL 活跃记录不会出现在在线成员列表中，因此不会把崩溃前的用户误判为在线。

注册/登录以 MySQL session 行创建成功作为提交点。若 MySQL 操作失败，SessionManager 删除预创建的框架 Session；若 MySQL 成功但 Redis 写入失败，仍返回登录成功，并由之后的读取触发缓存回填。

## 12. ARServer API

保留现有接口及响应语义：

| 方法 | 路径 | 说明 |
|---|---|---|
| POST | `/api/auth?username=...&password=...` | 注册或登录并返回 token |
| POST | `/api/session/enter?token=...&scene=...` | 进入场景 |
| POST | `/api/session/exit?token=...` | 退出场景 |
| GET | `/api/session?token=...` | 查询当前 Session |

增加弱协作和 AR 元数据接口：

| 方法 | 路径 | 说明 |
|---|---|---|
| POST | `/api/session/heartbeat?token=...` | 刷新在线时间和 Session TTL |
| GET | `/api/scenes/:sceneId/members?token=...` | 查询同场景在线成员 |
| GET | `/api/scenes` | 查询场景列表 |
| GET | `/api/scenes/:sceneId` | 查询 Marker 和模型资源元数据 |
| POST | `/api/scenes/:sceneId/interactions` | 固定返回结构化 501，预留交互入口 |

认证接口兼容当前查询参数形式，但访问日志不得记录明文密码。框架同时允许同一路径接收 JSON body，为后续 AR.js 页面迁移提供安全接口。

## 13. 异步数据库响应

EventLoop 线程不得调用 `future::get()`、`future::wait_for()` 或等待数据库、Redis 连接。数据库链路为：

```text
EventLoop 校验请求并创建异步响应上下文
  -> DBWorkerPool 执行阻塞 SQL
  -> 任务完成后调用原 EventLoop 的 queueInLoop()
  -> 在连接所属 EventLoop 构造并发送 HttpResponse
```

异步上下文只持有 `weak_ptr<TcpConnection>`。回到 EventLoop 后先检查连接是否仍存在，再发送响应。每个请求配置数据库超时；超时生成 503，并保证迟到的数据库结果不会二次发送响应。

AsyncResponder 同时保存本次请求已执行的中间件索引。异步响应回到 EventLoop 后，框架先逆序执行对应 `after()`，再序列化和发送响应，因此同步和异步路由具有一致的中间件语义。

现有 `DBWorkerPool::runSync()` 不进入 ARServer 请求链路。该接口既会阻塞调用线程，又在超时后存在任务继续访问已销毁 promise 的生命周期风险；实施时将其标记为测试兼容接口并把生产调用迁移为 `submit(task, completion)`。连接借用失败、SQL 异常和任务队列关闭都必须调用 completion 返回错误，不能静默丢弃任务。

同步 hiredis 调用同样不能发生在 EventLoop。ARServer 使用一个有界 CacheWorkerPool 执行 Redis 和 SessionCache 操作；队列满时快速返回 503。MySQL 继续由 DBWorkerPool 执行，两个工作池分别暴露队列深度和失败计数。

## 14. 静态资源与 AR.js 接入

框架重构期间，将当前 `www` 目录迁移到 `WebApps/ARServer/www`，三个前端文件内容保持逐字节不变。静态目录通过 ARServer 配置传入框架，不在框架源码中写死。

静态资源能力包括：

- 对规范化后的绝对根目录执行路径约束，拒绝目录穿越和非普通文件。
- 支持 HTML、CSS、JavaScript、JSON、PNG、JPG、SVG、`.patt`、`.glb`、`.gltf` 和 `.bin` MIME 类型。
- 小文件进入现有 LFU/Redis 两级缓存。
- 大文件绕过内存缓存并使用 `sendfile`。
- 支持 Last-Modified、ETag 和 304。
- Range 与 206 作为 AR.js 大模型优化，在 AR.js 页面开发阶段实现，不属于首轮框架重构验收条件。

AR.js 页面替换后的预期流程为：登录、选择场景、获取场景元数据、初始化摄像头与 Marker、加载模型、发送心跳、轮询成员、退出场景。正式部署必须提供 HTTPS；可由反向代理终止 TLS，或在第二阶段适配 Kama 的 OpenSSL 模块。

## 15. 错误处理、配置与安全

API 错误统一返回：

```json
{
  "status": "error",
  "code": "SESSION_EXPIRED",
  "message": "session expired",
  "request_id": "6f0c..."
}
```

状态码约定：400 参数或报文错误，401 认证失败，403 无权访问场景，404 不存在，405 方法不匹配，409 状态冲突，413 请求体过大，500 未处理异常，501 未实现的交互接口，503 关键依赖不可用。

配置从环境变量或配置文件加载，包括监听地址、线程数、静态目录、MySQL 地址和凭证、Redis 地址、缓存容量、请求大小限制和 Session TTL。源码中不得保留数据库密码。配置文件示例只包含非敏感占位值，真实凭证不进入 Git。

Redis 不可用时，Session 查询回源 MySQL；成员在线列表返回 503，因为在线状态只存在 Redis。MySQL 不可用时，允许只读 Session 查询返回仍在有效期内的缓存数据，注册、进入和退出等写操作返回 503。

## 16. 并发与生命周期约束

- 路由表和中间件链在启动前构建，启动后只读。
- 每条连接拥有独立 HttpContext，不使用跨线程共享解析状态。
- MemorySessionStorage 和其他共享容器必须显式同步或分片。
- DB、Redis 等阻塞操作不在 EventLoop 线程等待。
- 异步任务通过 weak_ptr 检查连接生命周期。
- 中间件实例默认视为多线程共享对象，不得保存单次请求的可变状态；请求相关状态放入 HttpRequest 或异步请求上下文。
- 定时清理和跨线程回调通过 EventLoop 的任务队列执行，不直接从外部线程操作连接。

## 17. 测试与性能验证

### 17.1 单元测试

- HTTP 请求行、头部、Body、分包、粘包、pipelining 和异常报文。
- OPTIONS、大小限制和不支持的 Transfer-Encoding。
- 静态路由、动态路由、具名参数、重复注册、404 和 405。
- 中间件正序 before、逆序 after、短路和异常转换。
- CORS 简单请求、合法/非法 Origin 和预检。
- Session 创建、续期、销毁、并发访问和安全 token 格式。
- 静态路径规范化、目录穿越、MIME、缓存校验和大文件分流。

### 17.2 集成测试

- 当前页面依赖的四个 API 行为保持兼容。
- 注册/登录、进入场景、查询 Session、心跳、成员列表和退出完整链路。
- 两个用户进入同一场景后能互相看到；停止心跳 30 秒后从成员列表消失。
- Redis 停止后 Session 查询回源 MySQL。
- MySQL 停止后写请求返回 503。
- 客户端提前断开后，数据库回调不访问已销毁连接。

### 17.3 工程与性能验证

- Debug 构建运行 ASan；并发相关测试运行 TSan。
- 比较改造前后静态文件 QPS、P50、P99 和错误率。
- 单独测量路由和中间件引入的开销。
- 记录 LFU、Redis 和 MySQL 三种读取路径的延迟与命中率。
- 压测报告包含硬件、编译参数、线程数、连接模式、文件大小、日志开关和完整命令。

## 18. 实施阶段划分

### 阶段一：框架应用层

扩展 HttpRequest/HttpResponse，移植 Router、MiddlewareChain 和 CORS，拆分框架构建目标，并为路由与中间件补齐单元测试。

### 阶段二：Session 与异步响应

移植并增强 Session 模块，实现可选的 `http_framework_redis` 适配库和 RedisSessionStorage，增加 AsyncResponder，消除 EventLoop 中对 MySQL 与 Redis 的等待，并补齐连接生命周期和超时测试。

### 阶段三：ARServer 重构

建立 `WebApps/ARServer`，迁移当前前端文件但保持内容不变，将 `main.cpp` 中的认证和场景逻辑拆分为 Handler、Service 和 Repository，增加心跳与成员列表。

### 阶段四：工程化与证据

完成错误模型、配置外置、测试、故障演练、性能回归和 README，产出可复现的演示与压测材料。

## 19. 验收标准

设计完成后的实现必须同时满足：

1. 独立程序只链接 `http_framework` 即可注册路由和中间件并启动 HTTP 服务。
2. `WebApps/ARServer` 不直接依赖 Channel、Socket 或 TcpConnection 等底层实现。
3. 当前三个前端文件内容保持不变，现有四个 API 行为兼容。
4. MySQL、Redis、DBWorkerPool、SessionDAO、SessionCache 和 TwoLevelCache 均被保留并有明确职责。
5. 两个用户进入同一场景后可通过 HTTP 轮询看到对方，停止心跳后按规则离线。
6. Redis 和 MySQL 故障路径能够通过脚本复现。
7. 框架单元测试和 AR 集成测试全部通过，ASan 无内存错误，TSan 目标用例无数据竞争。
8. 压测结果可通过仓库中的命令复现，并明确测试边界。
9. 源码中不存在明文数据库凭证，认证日志不包含密码。

## 20. 主要取舍

- 选择 HTTP 轮询而非 WebSocket，优先完成 HTTP 框架主线和可验证的多人能力。
- 选择保留 C++11，避免为移植 Kama 引入全项目语言标准升级。
- 选择 MySQL 作为业务状态最终数据源、Redis 作为缓存和在线状态源，避免把短生命周期心跳持续写入数据库。
- 选择框架 Session 与 AR 业务 Session 分层，避免 `scene_id` 等领域字段污染通用框架。
- 选择首版不内置 TLS，保持交付范围可控；AR.js 正式部署必须在反向代理或框架扩展层提供 HTTPS。
