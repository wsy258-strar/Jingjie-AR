# 境界AR全景门户 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 ARServer 升级为可浏览八个 WebP 全景、支持登录互动、点赞和评论持久化的“境界AR”门户。

**Architecture:** A C++11 `SceneCatalog` 提供固定的八个场景元数据。`SceneInteractionDAO` 通过现有 `DBWorkerPool` 异步保存和查询点赞/评论；`SceneInteractionService` 与异步 HTTP handler 将认证用户映射到数据库操作。前端由单页状态机管理首页、登录弹窗、A-Frame 全景页、心跳、互动和评论抽屉。

**Tech Stack:** C++11、现有 HTTP Framework/Router/Middleware、MySQL C API、DBWorkerPool、Redis Presence、原生 ES5 DOM API、A-Frame（固定版本的静态 vendor 文件）、WebP。

## Global Constraints

- 保持 C++11；不引入 Muduo、Node 运行时、前端构建工具或 WebSocket。
- 继续使用 Multi-Reactor、TcpServer、Buffer、TimerQueue、日志、内存池、MySQL、Redis、DBWorkerPool、SessionDAO、SessionCache、TwoLevelCache。
- EventLoop 禁止等待 MySQL、Redis、`future::get()` 或 `future::wait_for()`；所有新增 MySQL 操作经 `DBWorkerPool` 回调返回。
- 未登录者可浏览；点赞、取消点赞、评论必须带 `Authorization: Bearer <token>`。
- 兼容现有 `/api/auth?username=...&password=...` 与原有会话 API；新版前端认证只使用 JSON POST body，日志不得出现 password。
- 在线人数只统计已登录并持续心跳的用户；离开或 30 秒超时自动离线。
- 八个全景资源运行时从 `/assets/panoramas/` 静态提供，源 `media/8k16k_to_webp/` 不被应用修改。
- `music_url` 为空显示“音乐未配置”；非空时入场默认调用 `audio.play()`，被浏览器策略拒绝则转为手动播放按钮。
- 评论只显示用户名和内容；长度 1--300 字符；首版没有删除、回复、举报、审核或匿名访客在线统计。

---

## File Structure

| 文件 | 职责 |
|---|---|
| `WebApps/ARServer/include/catalog/SceneCatalog.h` | 八个场景元数据、ID 查找和 JSON 序列化所需访问器。 |
| `WebApps/ARServer/src/catalog/SceneCatalog.cpp` | 不可变场景目录与安全 URL 映射。 |
| `include/db/SceneInteractionDAO.h` / `src/db/SceneInteractionDAO.cpp` | 点赞、评论的预处理 SQL 与异步 DAO 回调。 |
| `WebApps/ARServer/include/services/SceneInteractionService.h` / `src/services/SceneInteractionService.cpp` | Session Token 验证、场景 ID 校验和 DAO 编排。 |
| `WebApps/ARServer/include/handlers/SceneInteractionHandlers.h` / `src/handlers/SceneInteractionHandlers.cpp` | 评论/点赞的异步 HTTP 适配与 JSON 响应。 |
| `WebApps/ARServer/src/handlers/SceneHandlers.cpp` | 用场景目录替换旧 5 个 Marker 场景的公共列表/详情接口。 |
| `WebApps/ARServer/src/ARServer.cpp` / `src/main.cpp` | 注册路由、构造交互服务并扩展 CORS DELETE。 |
| `WebApps/ARServer/src/handlers/AuthHandler.cpp` | 优先解析 JSON body，保持 query 参数兼容。 |
| `WebApps/ARServer/www/index.html` | 境界AR首页、认证弹窗、场景覆盖层、评论抽屉。 |
| `WebApps/ARServer/www/css/style.css` | 桌面四列/手机两列网格和全景控制层。 |
| `WebApps/ARServer/www/js/app.js` | 门户状态机、A-Frame 生命周期、心跳、互动、音乐。 |
| `WebApps/ARServer/www/vendor/aframe-1.6.0.min.js` | 固定版本 A-Frame 静态依赖。 |
| `WebApps/ARServer/www/assets/panoramas/*.webp` | 八个部署用全景图副本或由构建步骤建立的符号链接。 |
| `tests/unit/scene_catalog_test.cpp` | 目录、ID、素材 URL 与 JSON 规则。 |
| `tests/unit/scene_interaction_service_test.cpp` | 认证、点赞幂等、评论验证和异步失败。 |
| `tests/integration/jingjie_ar_api_test.sh` | HTTP 公开访问、认证、互动与兼容性。 |
| `tests/integration/jingjie_ar_static_test.sh` | 首页资源、八张图与 A-Frame 静态文件。 |
| `docs/sql/jingjie_ar_scene_interactions.sql` | 可重复执行的表迁移。 |

### Task 1: 固定八场景目录和静态素材交付

**Files:**
- Create: `WebApps/ARServer/include/catalog/SceneCatalog.h`
- Create: `WebApps/ARServer/src/catalog/SceneCatalog.cpp`
- Create: `tests/unit/scene_catalog_test.cpp`
- Create: `WebApps/ARServer/www/assets/panoramas/` 下八个 WebP 部署副本
- Modify: `WebApps/ARServer/src/handlers/SceneHandlers.cpp`
- Modify: `WebApps/ARServer/include/handlers/SceneHandlers.h`
- Modify: `src/http/StaticFileHandler.cpp`
- Modify: `tests/unit/static_file_handler_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `ar::SceneInfo { std::string id, name, panoramaUrl, musicUrl; }`。
- Produces `const SceneInfo* SceneCatalog::find(const std::string&)` 和 `const std::vector<SceneInfo>& SceneCatalog::all()`。

- [ ] **Step 1: Write the failing catalog test**

```cpp
CHECK(ar::SceneCatalog::all().size() == 8);
const ar::SceneInfo* scene = ar::SceneCatalog::find("golden-bay");
CHECK(scene && scene->name == "黄金海湾");
CHECK(scene->panoramaUrl == "/assets/panoramas/golden_bay_8k.webp");
CHECK(scene->musicUrl.empty());
CHECK(ar::SceneCatalog::find("unknown") == 0);
```

- [ ] **Step 2: Build the target to verify the test fails**

Run: `cmake --build build-full --target scene_catalog_test -j2`  
Expected: compilation fails because `catalog/SceneCatalog.h` does not exist.

- [ ] **Step 3: Implement the immutable catalog and public JSON handlers**

```cpp
const std::vector<SceneInfo>& SceneCatalog::all();
const SceneInfo* SceneCatalog::find(const std::string& id);
// SceneHandlers::list emits all IDs, Chinese names, panorama_url and music_url.
// SceneHandlers::get returns 404 JSON when SceneCatalog::find() returns null.
```

Map exactly the eight files and names from the design document. Add `SceneCatalog.cpp` to `ar_server` and `scene_catalog_test` sources. Copy the eight generated WebPs into `www/assets/panoramas/` without re-encoding them. Add a `.webp` branch in `StaticFileHandler::mime()` returning `image/webp`, and add `asset.webp` coverage to `static_file_handler_test`.

- [ ] **Step 4: Run focused tests**

Run: `cmake --build build-full --target scene_catalog_test ar_handlers_test -j2 && ctest --test-dir build-full -R 'scene_catalog_test|ar_handlers_test' --output-on-failure`  
Expected: both tests pass and `/api/scenes` no longer contains Marker or model URLs.

- [ ] **Step 5: Commit**

```bash
git add WebApps/ARServer/include/catalog WebApps/ARServer/src/catalog \
  WebApps/ARServer/src/handlers/SceneHandlers.cpp WebApps/ARServer/include/handlers/SceneHandlers.h \
  WebApps/ARServer/www/assets/panoramas src/http/StaticFileHandler.cpp \
  tests/unit/scene_catalog_test.cpp tests/unit/static_file_handler_test.cpp CMakeLists.txt
git commit -m "feat: add Jingjie panorama scene catalog"
```

### Task 2: 点赞和评论异步 DAO 与 SQL 迁移

**Files:**
- Create: `include/db/SceneInteractionDAO.h`
- Create: `src/db/SceneInteractionDAO.cpp`
- Create: `docs/sql/jingjie_ar_scene_interactions.sql`
- Create: `tests/unit/scene_interaction_dao_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `SceneLikeSummary { uint64_t count; bool likedByRequester; }`。
- Produces `SceneComment { uint64_t id; std::string username; std::string content; }`。
- Produces asynchronous DAO methods `like`, `unlike`, `summary`, `listComments`, `createComment`, each taking a callback and never returning DB data synchronously.

- [ ] **Step 1: Write failing unavailable-pool DAO contract tests**

```cpp
SceneInteractionDAO dao(0);
dao.like("golden-bay", 7, [&](bool changed, uint64_t count) {
    CHECK(!changed); CHECK(count == 0); ++callbacks;
});
dao.createComment("golden-bay", 7, "hello", [&](uint64_t id) {
    CHECK(id == 0); ++callbacks;
});
CHECK(callbacks == 2);
```

- [ ] **Step 2: Run the new target and confirm it fails**

Run: `cmake --build build-full --target scene_interaction_dao_test -j2`  
Expected: missing `SceneInteractionDAO` symbols.

- [ ] **Step 3: Implement parameterized asynchronous SQL**

Use the migration SQL from the specification. Use `INSERT IGNORE INTO scene_likes` for idempotent likes, `DELETE FROM scene_likes` for unlike, `SELECT COUNT(*)`, and `INSERT INTO scene_comments`. List comments with `JOIN users ON users.id = scene_comments.user_id`, `WHERE scene_id = ? AND id < ?`, `ORDER BY id DESC LIMIT ?`. Bind all user data via `MYSQL_BIND`; capture callback state by value/shared pointer.

- [ ] **Step 4: Run DAO tests and MySQL-backed regression tests**

Run: `cmake --build build-full --target scene_interaction_dao_test session_dao_test -j2 && ctest --test-dir build-full -R 'scene_interaction_dao_test|session_dao_test' --output-on-failure`  
Expected: all pass; no test uses `sleep` on the EventLoop thread.

- [ ] **Step 5: Commit**

```bash
git add include/db/SceneInteractionDAO.h src/db/SceneInteractionDAO.cpp \
  docs/sql/jingjie_ar_scene_interactions.sql tests/unit/scene_interaction_dao_test.cpp CMakeLists.txt
git commit -m "feat: persist panorama likes and comments"
```

### Task 3: 交互服务、认证边界与异步 API handlers

**Files:**
- Create: `WebApps/ARServer/include/services/SceneInteractionService.h`
- Create: `WebApps/ARServer/src/services/SceneInteractionService.cpp`
- Create: `WebApps/ARServer/include/handlers/SceneInteractionHandlers.h`
- Create: `WebApps/ARServer/src/handlers/SceneInteractionHandlers.cpp`
- Modify: `WebApps/ARServer/src/middleware/AuthMiddleware.cpp`
- Modify: `WebApps/ARServer/src/ARServer.cpp`
- Modify: `WebApps/ARServer/src/main.cpp`
- Modify: `include/http/HttpServer.h`
- Modify: `include/http/HttpDispatcher.h`
- Modify: `src/http/HttpDispatcher.cpp`
- Create: `tests/unit/scene_interaction_service_test.cpp`
- Modify: `tests/unit/http_dispatcher_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes `SessionService::get(token, callback)`, `SceneCatalog::find`, and `SceneInteractionDAO`.
- Produces `SceneInteractionService::like(token, sceneId, callback)`, `unlike`, `listComments(sceneId, beforeId, limit, callback)`, `comment(token, sceneId, content, callback)`.
- Produces handlers `detail`, `like`, `unlike`, `comments`, `comment` with `AsyncResponder`.
- Defines `enum SceneInteractionStatus { kOk, kUnauthorized, kBadRequest, kNotFound, kUnavailable };` and `struct SceneInteractionResult { SceneInteractionStatus status; uint64_t likeCount; bool liked; std::vector<SceneComment> comments; uint64_t nextBefore; };`.

- [ ] **Step 1: Write failing service/handler tests**

```cpp
service.comment("valid-token", "golden-bay", "", callback);
CHECK(result.status == ar::SceneInteractionResult::kBadRequest);
service.like("", "golden-bay", callback);
CHECK(result.status == ar::SceneInteractionResult::kUnauthorized);
service.comment("valid-token", "not-a-scene", "hello", callback);
CHECK(result.status == ar::SceneInteractionResult::kNotFound);

HttpDispatcher dispatcher;
dispatcher.DeleteAsync("/likes/:sceneId", [](const HttpRequest&, const AsyncResponder& reply) {
    HttpResponse response(false); response.setStatusCode(HttpResponse::k200Ok); reply.send(response);
});
```

- [ ] **Step 2: Run the focused target and confirm failure**

Run: `cmake --build build-full --target scene_interaction_service_test http_dispatcher_test -j2`  
Expected: missing service/handler types and missing `DeleteAsync`.

- [ ] **Step 3: Implement authorization and route registration**

Validate a Bearer Token through the existing session service before DAO writes. Return `401`, `400`, `404`, `503` through `makeApiError`; only successful calls return JSON. Permit public `GET /api/scenes/:id/comments` and `GET /members`; require auth for `POST/DELETE /likes` and `POST /comments`. Register:

```cpp
server_.GetAsync("/api/scenes/:sceneId/comments", comments);
server_.GetAsync("/api/scenes/:sceneId", detail);
server_.PostAsync("/api/scenes/:sceneId/likes", like);
server_.DeleteAsync("/api/scenes/:sceneId/likes", unlike);
server_.PostAsync("/api/scenes/:sceneId/comments", comment);
```

Implement `DeleteAsync` in `HttpDispatcher` using `router_.addAsync(HttpRequest::kDelete, pattern, callback)` and expose it through `HttpServer`. Extend CORS allowed methods with `DELETE`. Wire the new DAO/service in `main.cpp` only when MySQL is present; otherwise writes respond `503`.

- [ ] **Step 4: Run unit and middleware regressions**

Run: `cmake --build build-full --target scene_interaction_service_test request_middleware_test ar_handlers_test -j2 && ctest --test-dir build-full -R 'scene_interaction_service_test|request_middleware_test|ar_handlers_test' --output-on-failure`  
Expected: public reads pass without token; writes reject absent token and never block an EventLoop.

- [ ] **Step 5: Commit**

```bash
git add include/http/HttpServer.h include/http/HttpDispatcher.h src/http/HttpDispatcher.cpp \
  tests/unit/http_dispatcher_test.cpp WebApps/ARServer/include/services/SceneInteractionService.h \
  WebApps/ARServer/src/services/SceneInteractionService.cpp \
  WebApps/ARServer/include/handlers/SceneInteractionHandlers.h \
  WebApps/ARServer/src/handlers/SceneInteractionHandlers.cpp \
  WebApps/ARServer/src/middleware/AuthMiddleware.cpp WebApps/ARServer/src/ARServer.cpp \
  WebApps/ARServer/src/main.cpp tests/unit/scene_interaction_service_test.cpp CMakeLists.txt
git commit -m "feat: expose authenticated panorama interactions"
```

### Task 4: JSON 登录与密码日志回归保护

**Files:**
- Modify: `WebApps/ARServer/src/handlers/AuthHandler.cpp`
- Modify: `WebApps/ARServer/include/handlers/AuthHandler.h`
- Modify: `WebApps/ARServer/src/middleware/AuthMiddleware.cpp`
- Modify: `tests/unit/ar_handlers_test.cpp`
- Create: `tests/integration/auth_body_and_log_test.sh`

**Interfaces:**
- Produces `AuthHandler::credentials(const HttpRequest&, std::string* username, std::string* password)`; JSON body takes precedence, query parameters remain fallback.

- [ ] **Step 1: Add failing JSON-body and compatibility tests**

```cpp
HttpRequest bodyRequest;
bodyRequest.setBody("{\"username\":\"alice\",\"password\":\"secret\"}");
CHECK(ar::AuthHandler::credentials(bodyRequest, &username, &password));
CHECK(password == "secret");
HttpRequest queryRequest; queryRequest.setQuery("username=alice&password=secret");
CHECK(ar::AuthHandler::credentials(queryRequest, &username, &password));
```

- [ ] **Step 2: Verify failure before implementation**

Run: `cmake --build build-full --target ar_handlers_test -j2`  
Expected: compilation fails for missing `credentials`.

- [ ] **Step 3: Implement a minimal JSON string parser and log redaction**

Accept only a JSON object containing nonempty string `username` and `password`; reject malformed JSON with existing 400 response. Do not add a third-party JSON library. Keep query fallback for compatibility. Update `AccessLogMiddleware` or request logging so any query key named `password` is rendered as `password=***`.

- [ ] **Step 4: Run authentication and integration checks**

Run: `cmake --build build-full --target ar_handlers_test -j2 && ctest --test-dir build-full -R ar_handlers_test --output-on-failure && BASE_URL=http://127.0.0.1:8080 tests/integration/auth_body_and_log_test.sh`  
Expected: JSON login succeeds, old query login succeeds, and captured logs contain no supplied password.

- [ ] **Step 5: Commit**

```bash
git add WebApps/ARServer/src/handlers/AuthHandler.cpp WebApps/ARServer/include/handlers/AuthHandler.h \
  WebApps/ARServer/src/middleware/AuthMiddleware.cpp tests/unit/ar_handlers_test.cpp \
  tests/integration/auth_body_and_log_test.sh
git commit -m "fix: accept body auth without logging passwords"
```

### Task 5: 境界AR前端门户与 A-Frame 生命周期

**Files:**
- Modify: `WebApps/ARServer/www/index.html`
- Modify: `WebApps/ARServer/www/css/style.css`
- Modify: `WebApps/ARServer/www/js/app.js`
- Create: `WebApps/ARServer/www/vendor/aframe-1.6.0.min.js`
- Create: `tests/integration/jingjie_ar_static_test.sh`

**Interfaces:**
- Consumes public scenes API, authenticated interaction API, `/api/session/{enter,exit,heartbeat}`, `/api/scenes/:id/members`.
- Produces browser functions `openScene(scene)`, `closeScene()`, `toggleLike()`, `openComments()`, `submitComment()`, `toggleMusic()`, `toggleFullscreen()`.

- [ ] **Step 1: Write failing static/UI contract test**

```bash
curl -fsS "$BASE_URL/" | grep -F '境界AR · 360° 全景探索'
curl -fsS "$BASE_URL/vendor/aframe-1.6.0.min.js" | grep -F 'AFRAME'
for file in docklands_02_8k golden_bay_8k graaff_reinet_groote_kerk_8k; do
  curl -fsSI "$BASE_URL/assets/panoramas/$file.webp" | grep -qi '^Content-Type: image/webp'
done
```

- [ ] **Step 2: Run it before implementation**

Run: `BASE_URL=http://127.0.0.1:8080 tests/integration/jingjie_ar_static_test.sh`  
Expected: fails because the new title, vendor file and panorama paths do not exist.

- [ ] **Step 3: Replace demo UI with the portal state machine**

Implement these rules exactly:

```javascript
function authHeaders() {
  return currentToken ? { 'Authorization': 'Bearer ' + currentToken } : {};
}
function requireLogin() {
  if (currentToken) return true;
  showToast('请先登录后再进行点赞/评论'); openAuthModal(); return false;
}
function closeScene() {
  stopHeartbeat(); stopMusic(); destroyAFrameScene(); hideSceneOverlay();
  if (currentToken) post('/api/session/exit', {}, authHeaders());
}
```

Render four columns at desktop widths and two columns at `max-width: 640px`. On card click construct one `a-scene` containing one `a-sky` with `src` equal to the catalog `panorama_url`; do not request camera or load AR.js. The controls must be right-top online count, right-under-header full-screen/music, right-bottom like/comment, bottom comment drawer, and left-bottom exit. Use `textContent` for comments. Add `popstate` cleanup and interval cleanup.

- [ ] **Step 4: Run static test and manual interaction checklist**

Run: `BASE_URL=http://127.0.0.1:8080 tests/integration/jingjie_ar_static_test.sh`  
Expected: PASS. Manually verify Chrome desktop drag, mobile emulation two-column grid, fullscreen, comment drawer and guest login prompt.

- [ ] **Step 5: Commit**

```bash
git add WebApps/ARServer/www/index.html WebApps/ARServer/www/css/style.css \
  WebApps/ARServer/www/js/app.js WebApps/ARServer/www/vendor/aframe-1.6.0.min.js \
  tests/integration/jingjie_ar_static_test.sh
git commit -m "feat: build Jingjie AR panorama portal"
```

### Task 6: 端到端互动、在线人数与回归验收

**Files:**
- Create: `tests/integration/jingjie_ar_api_test.sh`
- Modify: `tests/integration/ar_api_test.sh`
- Modify: `README.md`

**Interfaces:**
- Consumes the final API routes and test MySQL/Redis server configuration.
- Produces reproducible commands for migration, static assets and browser use.

- [ ] **Step 1: Write failing end-to-end test**

```bash
guest_status=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$BASE_URL/api/scenes/golden-bay/likes")
test "$guest_status" = 401
token_a=$(register_user "jingjie-a")
token_b=$(register_user "jingjie-b")
like_scene "$token_a" golden-bay
comment_scene "$token_a" golden-bay '沉浸感很好'
assert_member_count_after_heartbeat "$token_a" "$token_b" golden-bay 2
```

- [ ] **Step 2: Run it to prove failure before final integration work**

Run: `BASE_URL=http://127.0.0.1:8080 tests/integration/jingjie_ar_api_test.sh`  
Expected: fails until migration is applied and all routes are wired.

- [ ] **Step 3: Add reproducible setup and complete any integration wiring**

Document `mysql < docs/sql/jingjie_ar_scene_interactions.sql`, copying panorama assets, `source .env.arserver`, build, and launch commands. Update compatibility integration test to assert old 5-scene/session routes still return their documented response or explicitly revise its scene fixture to a valid new catalog ID while preserving endpoint semantics.

- [ ] **Step 4: Run complete verification**

Run:

```bash
cmake -S . -B build-full -DCMAKE_BUILD_TYPE=Debug
cmake --build build-full -j2
ctest --test-dir build-full --output-on-failure
BASE_URL=http://127.0.0.1:8080 tests/integration/ar_api_test.sh
BASE_URL=http://127.0.0.1:8080 tests/integration/jingjie_ar_api_test.sh
BASE_URL=http://127.0.0.1:8080 tests/integration/jingjie_ar_static_test.sh
```

Expected: all tests pass; guests cannot mutate; likes/comments survive a new request; two logged-in clients count as online and count decreases after exit/timeout.

- [ ] **Step 5: Commit**

```bash
git add tests/integration/jingjie_ar_api_test.sh tests/integration/ar_api_test.sh README.md
git commit -m "test: verify Jingjie AR collaboration portal"
```

## Plan Self-Review

- **Spec coverage:** Tasks 1 and 5 cover all eight assets, desktop/mobile layout, A-Frame controls, full screen and music fallback. Tasks 2--4 cover persistent interactions, authorization, JSON login and no password logs. Task 6 covers online presence, integration and reproducibility.
- **No-placeholder scan:** Every task has exact paths, interfaces, failing test, command, implementation boundary and commit command.
- **Consistency:** All public scene IDs come from `SceneCatalog`; all interaction routes use `:sceneId`; all writes use Bearer tokens and asynchronous handlers; only the existing session heartbeat supplies online identity.
