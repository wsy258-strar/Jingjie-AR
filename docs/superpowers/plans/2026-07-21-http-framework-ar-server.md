# Embeddable C++ HTTP Framework and ARServer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the existing WebServer into a linkable C++11 HTTP framework and rebuild the current AR scene service as WebApps/ARServer with weak multi-user collaboration.

**Architecture:** Preserve the project's Multi-Reactor network core and data infrastructure. Add Router, Middleware, CORS, generic Session, and asynchronous response abstractions above HttpServer; move AR-specific handlers and services into WebApps/ARServer, with MySQL as the durable source, Redis as cache and presence storage, and the current frontend bytes unchanged.

**Tech Stack:** Linux, C++11, CMake, epoll, pthread, MySQL C API, hiredis/Redis, CTest, ASan, TSan, wrk, curl.

## Global Constraints

- Keep the project at C++11; do not introduce C++17 structured bindings, std::any, or std::make_unique.
- Preserve the existing Reactor, TcpServer, Buffer, TimerQueue, logging, memory pool, MySQL, Redis, DBWorkerPool, SessionDAO, SessionCache, and TwoLevelCache implementations unless a task explicitly hardens their interface.
- Reuse only the authorized Router, Middleware, Session, and CORS portions of Kama-HTTPServer; do not add Muduo as a dependency.
- Name the application directory WebApps, not examples.
- Move www/index.html, www/css/style.css, and www/js/app.js without changing their bytes.
- Keep the four existing AR API paths and response fields compatible.
- Implement weak collaboration with HTTP heartbeat and polling; do not add WebSocket.
- Do not add TLS in this plan; document HTTPS as required for the later AR.js deployment.
- Never block an EventLoop thread on MySQL, Redis, future::get, or future::wait_for.
- Do not commit database credentials or log the password query parameter.
- Use TDD for every behavioral task and commit after every task.

## Baseline and File Map

Baseline command verified on 2026-07-21:

~~~bash
cmake -S . -B /tmp/webserver-plan-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/webserver-plan-build -j2
ctest --test-dir /tmp/webserver-plan-build --output-on-failure
~~~

Expected baseline: build succeeds and the existing session_cache_test is the only CTest test and passes.

New framework files:

- include/http/HttpLimits.h: immutable request parsing limits.
- include/http/AsyncResponder.h and src/http/AsyncResponder.cpp: exactly-once asynchronous response handle.
- include/router/RouterHandler.h, include/router/Router.h, src/router/Router.cpp: static and dynamic routing.
- include/middleware/Middleware.h, include/middleware/MiddlewareChain.h, src/middleware/MiddlewareChain.cpp: middleware contract and execution.
- include/middleware/cors/CorsConfig.h, include/middleware/cors/CorsMiddleware.h, src/middleware/cors/CorsMiddleware.cpp: CORS behavior.
- include/middleware/RequestIdMiddleware.h and include/middleware/AccessLogMiddleware.h with matching sources: common request observability.
- include/session/Session.h, include/session/SessionStorage.h, include/session/SessionManager.h and matching src/session files: generic Session lifecycle.
- include/session/RedisSessionStorage.h and src/session/RedisSessionStorage.cpp: optional hiredis adapter.
- include/base/TaskWorkerPool.h and src/base/TaskWorkerPool.cpp: bounded worker pool for synchronous Redis operations.
- include/http/StaticFileHandler.h and src/http/StaticFileHandler.cpp: reusable static file handler.

New AR application files:

- WebApps/ARServer/include/ARServer.h and WebApps/ARServer/src/ARServer.cpp: dependency composition and route registration.
- WebApps/ARServer/include/config/AppConfig.h and WebApps/ARServer/src/config/AppConfig.cpp: environment configuration.
- WebApps/ARServer/include/services/AuthService.h and WebApps/ARServer/src/services/AuthService.cpp: register/login pipeline.
- WebApps/ARServer/include/services/SessionService.h and WebApps/ARServer/src/services/SessionService.cpp: query, enter, and exit operations.
- WebApps/ARServer/include/services/PresenceService.h and WebApps/ARServer/src/services/PresenceService.cpp: heartbeat and scene membership.
- WebApps/ARServer/include/services/ArSessionValidator.h and WebApps/ARServer/src/services/ArSessionValidator.cpp: asynchronous Redis-to-MySQL token validation.
- WebApps/ARServer/include/handlers/AuthHandler.h and WebApps/ARServer/src/handlers/AuthHandler.cpp: HTTP auth adapter.
- WebApps/ARServer/include/handlers/SessionHandlers.h and WebApps/ARServer/src/handlers/SessionHandlers.cpp: HTTP Session adapters.
- WebApps/ARServer/include/handlers/SceneHandlers.h and WebApps/ARServer/src/handlers/SceneHandlers.cpp: members and metadata adapters.
- WebApps/ARServer/src/main.cpp: process bootstrap only.
- WebApps/ARServer/www: byte-identical move of the current www directory.

Test support:

- tests/TestSupport.h: CHECK macro and Buffer request helper.
- tests/unit: framework unit executables.
- tests/integration: AR API and failure scripts.

---

### Task 1: HTTP Model and Parser Boundaries

**Files:**
- Create: include/http/HttpLimits.h
- Create: tests/TestSupport.h
- Create: tests/unit/http_protocol_test.cpp
- Modify: include/http/HttpRequest.h
- Modify: src/http/HttpRequest.cpp
- Modify: include/http/HttpResponse.h
- Modify: src/http/HttpResponse.cpp
- Modify: include/http/HttpContext.h
- Modify: src/http/HttpContext.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- Consumes: Buffer, Timestamp, and current HttpContext incremental state.
- Produces: HttpRequest::kOptions, pathParameter(), setPathParameter(), cookie(), setAttribute(), attribute(), HttpResponse status getters, HttpLimits, and HttpContext::ParseError.

- [ ] **Step 1: Add the protocol test executable with failing coverage**

Create tests/TestSupport.h:

~~~cpp
#pragma once
#include <cstdlib>
#include <iostream>
#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #expr << std::endl; \
    std::exit(1); \
} } while (0)
~~~

Create tests/unit/http_protocol_test.cpp with cases that parse OPTIONS, read a cookie, preserve a named path parameter, reject a request line over 8192 bytes, reject headers over 32768 bytes, reject a body over 1048576 bytes, and report kUnsupportedTransferEncoding for Transfer-Encoding: chunked. Use Buffer::append(), HttpContext::parseRequest(), gotAll(), and error().

Add a CTest target named http_protocol_test that links the existing src_lib and memory_lib targets. Tasks 1 through 8 keep src_lib as a compatibility build target; Task 9 introduces the final http_framework/ar_data split after the application has a new home.

- [ ] **Step 2: Run the test and verify the new API is missing**

Run:

~~~bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target http_protocol_test -j2
~~~

Expected: compilation fails because kOptions, HttpLimits, pathParameter(), cookie(), attribute(), and ParseError do not exist.

- [ ] **Step 3: Add the exact public model interfaces**

Create include/http/HttpLimits.h:

~~~cpp
#pragma once
#include <cstddef>
struct HttpLimits {
    size_t maxRequestLineBytes;
    size_t maxHeaderBytes;
    size_t maxBodyBytes;
    HttpLimits() : maxRequestLineBytes(8192), maxHeaderBytes(32768),
                   maxBodyBytes(1024 * 1024) {}
};
~~~

Extend HttpRequest::Method with kOptions. Add:

~~~cpp
void setPathParameter(const std::string& name, const std::string& value);
std::string pathParameter(const std::string& name) const;
std::string cookie(const std::string& name) const;
void setAttribute(const std::string& name, const std::string& value);
std::string attribute(const std::string& name) const;
~~~

Back these methods with two unordered maps and include both maps in swap(). Extend methodString() with OPTIONS. Extend HttpResponse with 204, 401, 403, 405, 409, 413, 501, and 503 plus:

~~~cpp
HttpStatusCode statusCode() const;
const std::string& body() const;
std::string header(const std::string& name) const;
~~~

- [ ] **Step 4: Implement parser limits and explicit parse errors**

Add to HttpContext:

~~~cpp
enum ParseError {
    kParseOk,
    kBadRequest,
    kRequestLineTooLarge,
    kHeadersTooLarge,
    kBodyTooLarge,
    kUnsupportedTransferEncoding
};
explicit HttpContext(const HttpLimits& limits = HttpLimits());
ParseError error() const;
void setLimits(const HttpLimits& limits);
~~~

Track bytes consumed while reading headers. Reject request lines before CRLF when readable bytes exceed maxRequestLineBytes. Parse Content-Length with a checked decimal conversion instead of std::stoul, reject overflow and values above maxBodyBytes, and reject every non-empty Transfer-Encoding value. Reset error and counters in reset().

- [ ] **Step 5: Run protocol tests**

Run:

~~~bash
cmake --build build --target http_protocol_test -j2
ctest --test-dir build -R '^http_protocol_test$' --output-on-failure
~~~

Expected: one test passes.

- [ ] **Step 6: Commit**

~~~bash
git add CMakeLists.txt include/http src/http tests/TestSupport.h tests/unit/http_protocol_test.cpp
git commit -m "feat: harden HTTP request model and parser"
~~~

### Task 2: Static and Dynamic Router

**Files:**
- Create: include/router/RouterHandler.h
- Create: include/router/Router.h
- Create: src/router/Router.cpp
- Create: tests/unit/router_test.cpp
- Modify: CMakeLists.txt
- Modify: src/CMakeLists.txt

**Interfaces:**
- Consumes: mutable HttpRequest path parameters and HttpResponse.
- Produces: http::router::Router, HandlerCallback, AsyncHandlerCallback registration slots, and MatchResult.

- [ ] **Step 1: Write failing router tests**

Create tests/unit/router_test.cpp covering:

~~~cpp
http::router::Router router;
router.add(HttpRequest::kGet, "/health", healthHandler);
router.add(HttpRequest::kGet, "/api/scenes/:sceneId/members", membersHandler);

HttpRequest request;
request.setMethod(HttpRequest::kGet);
request.setPath("/api/scenes/scene-2/members");
HttpResponse response(false);
http::router::MatchResult result = router.route(request, &response);
CHECK(result == http::router::kHandled);
CHECK(request.pathParameter("sceneId") == "scene-2");
~~~

Also assert exact route precedence over dynamic routes, duplicate registration returns false, an unknown path returns kNotFound, and POST to a GET-only path returns kMethodNotAllowed with allowedMethods containing GET.

- [ ] **Step 2: Run the test and verify Router is absent**

Run:

~~~bash
cmake --build build --target router_test -j2
~~~

Expected: compilation fails because include/router/Router.h does not exist.

- [ ] **Step 3: Port and adapt the Router interfaces**

Keep AsyncResponder in the global namespace beside HttpRequest/HttpResponse. Forward-declare it before opening namespace http::router, then define:

~~~cpp
class AsyncResponder;
namespace http { namespace router {
using HandlerCallback = std::function<void(const HttpRequest&, HttpResponse*)>;
using AsyncHandlerCallback =
    std::function<void(const HttpRequest&, const ::AsyncResponder&)>;

enum MatchResult { kHandled, kAsyncPending, kNotFound, kMethodNotAllowed };

bool add(HttpRequest::Method method, const std::string& pattern,
         const HandlerCallback& handler);
bool addAsync(HttpRequest::Method method, const std::string& pattern,
              const AsyncHandlerCallback& handler);
MatchResult route(HttpRequest& request, HttpResponse* response,
                  const AsyncResponder* responder = 0) const;
const std::vector<HttpRequest::Method>& allowedMethods() const;
} }
~~~

RouterHandler exposes a virtual handle(const HttpRequest&, HttpResponse*) method and a virtual destructor. Store the names captured from every :name segment next to the compiled regex. Do not copy Kama's param1 naming or structured bindings.

- [ ] **Step 4: Implement deterministic matching**

Check the exact map first. Then inspect dynamic routes in registration order, populate request path parameters by name, and invoke the copied request only after parameter injection. When a path matches under another method, return kMethodNotAllowed and record all allowed methods. Reject duplicate method/pattern pairs instead of overwriting.

- [ ] **Step 5: Run router tests**

Run:

~~~bash
cmake --build build --target router_test -j2
ctest --test-dir build -R '^router_test$' --output-on-failure
~~~

Expected: one test passes.

- [ ] **Step 6: Commit**

~~~bash
git add CMakeLists.txt src/CMakeLists.txt include/router src/router tests/unit/router_test.cpp
git commit -m "feat: add static and dynamic HTTP router"
~~~

### Task 3: Middleware Chain and Correct CORS

**Files:**
- Create: include/middleware/Middleware.h
- Create: include/middleware/MiddlewareChain.h
- Create: src/middleware/MiddlewareChain.cpp
- Create: include/middleware/cors/CorsConfig.h
- Create: include/middleware/cors/CorsMiddleware.h
- Create: src/middleware/cors/CorsMiddleware.cpp
- Create: tests/unit/middleware_test.cpp
- Modify: CMakeLists.txt
- Modify: src/CMakeLists.txt

**Interfaces:**
- Consumes: HttpRequest headers/method and HttpResponse status/headers.
- Produces: Middleware::before/after, MiddlewareChain::processBefore/processAfter, CorsConfig, and CorsMiddleware.

- [ ] **Step 1: Write failing execution-order and CORS tests**

Use a RecordingMiddleware that pushes strings into a shared vector. Assert:

~~~cpp
CHECK(chain.processBefore(request, response, executed));
chain.processAfter(request, response, executed);
CHECK(events == std::vector<std::string>({"a.before", "b.before",
                                         "b.after", "a.after"}));
~~~

Add a middleware whose before() returns false and assert later middleware and the route are skipped. Add CORS tests for a valid preflight returning 204, a rejected origin returning 403, wildcard without credentials, and allowCredentials=true echoing the request Origin plus Vary: Origin.

- [ ] **Step 2: Run the test and verify middleware headers are missing**

Run:

~~~bash
cmake --build build --target middleware_test -j2
~~~

Expected: compilation fails because include/middleware/MiddlewareChain.h is absent.

- [ ] **Step 3: Add the middleware contract**

Define:

~~~cpp
class Middleware {
public:
    virtual ~Middleware() {}
    virtual bool before(HttpRequest& request, HttpResponse& response) = 0;
    virtual void after(const HttpRequest& request, HttpResponse& response) = 0;
};
~~~

MiddlewareChain stores shared_ptr instances. processBefore fills an executed vector only after a middleware's before() returns true. If before() returns false, include that middleware in executed so its after() still runs, stop the request, and return false. processAfter iterates executed in reverse order.

- [ ] **Step 4: Port CORS without exception control flow**

CorsConfig contains allowedOrigins, allowedMethods, allowedHeaders, allowCredentials, and maxAge. CorsMiddleware::before handles OPTIONS, validates Origin and Access-Control-Request-Method, writes 204 or 403, and returns false. after reads the current request Origin, never stores per-request state in the shared middleware object, adds Vary: Origin for a whitelist, and rejects wildcard plus credentials by echoing only a validated Origin.

- [ ] **Step 5: Run middleware tests**

Run:

~~~bash
cmake --build build --target middleware_test -j2
ctest --test-dir build -R '^middleware_test$' --output-on-failure
~~~

Expected: one test passes.

- [ ] **Step 6: Commit**

~~~bash
git add CMakeLists.txt src/CMakeLists.txt include/middleware src/middleware tests/unit/middleware_test.cpp
git commit -m "feat: add middleware chain and CORS"
~~~

### Task 4: Dispatcher and Synchronous HttpServer API

**Files:**
- Create: include/http/HttpDispatcher.h
- Create: src/http/HttpDispatcher.cpp
- Create: tests/unit/http_dispatcher_test.cpp
- Modify: include/http/HttpServer.h
- Modify: src/http/HttpServer.cpp
- Modify: include/net/TcpConnection.h
- Modify: CMakeLists.txt
- Modify: src/CMakeLists.txt

**Interfaces:**
- Consumes: Router, MiddlewareChain, HttpLimits, HttpRequest, and HttpResponse.
- Produces: HttpDispatcher::Get/Post/Put/Delete/Options, HttpServer route proxies, addMiddleware(), setLimits(), and a pipelining-safe onMessage loop.

- [ ] **Step 1: Write failing dispatcher tests**

Create an HttpDispatcher, register GET /hello, add two RecordingMiddleware instances, and assert that dispatch returns 200 with the expected body and before/after order. Add cases for 404 and POST /hello returning 405 with Allow: GET. Add an OPTIONS route case.

Use this public surface:

~~~cpp
HttpDispatcher dispatcher;
CHECK(dispatcher.Get("/hello", [](const HttpRequest&, HttpResponse* response) {
    response->setBody("hello");
}));
dispatcher.addMiddleware(first);
HttpDispatcher::Result result = dispatcher.dispatch(request, &response);
CHECK(result == HttpDispatcher::kComplete);
~~~

- [ ] **Step 2: Run the test and verify HttpDispatcher is absent**

Run:

~~~bash
cmake --build build --target http_dispatcher_test -j2
~~~

Expected: compilation fails because include/http/HttpDispatcher.h does not exist.

- [ ] **Step 3: Implement HttpDispatcher**

HttpDispatcher owns Router and MiddlewareChain. Define:

~~~cpp
enum Result { kComplete, kAsyncPending };
bool Get(const std::string&, const HttpCallback&);
bool Post(const std::string&, const HttpCallback&);
bool Put(const std::string&, const HttpCallback&);
bool Delete(const std::string&, const HttpCallback&);
bool Options(const std::string&, const HttpCallback&);
void addMiddleware(const std::shared_ptr<http::middleware::Middleware>&);
void setFallback(const HttpCallback&);
Result dispatch(HttpRequest& request, HttpResponse* response);
~~~

dispatch must create an executed middleware vector, short-circuit when processBefore returns false, convert Router kNotFound to 404, convert kMethodNotAllowed to 405 plus Allow, catch std::exception as 500 JSON, and always run processAfter for executed middleware.

- [ ] **Step 4: Expose the framework-style API from HttpServer**

Add Get/Post/Put/Delete/Options, addMiddleware, and setFallback methods that delegate to dispatcher_. Keep setHttpCallback as a compatibility alias for setFallback. Add setLimits(const HttpLimits&).

On connection establishment call conn->getContext().setLimits(limits_). Change onRequest to copy the parsed request into a mutable request before dispatch.

- [ ] **Step 5: Make onMessage drain all complete requests**

Replace the one-shot parse branch with:

~~~cpp
while (buf->readableBytes() > 0) {
    if (!context.parseRequest(buf, receiveTime)) {
        sendParseError(conn, context.error());
        return;
    }
    if (!context.gotAll()) {
        return;
    }
    onRequest(conn, context.request());
    context.reset();
}
~~~

Map line/header/body size errors to 413, unsupported transfer encoding to 501, and malformed syntax to 400. Stop parsing when an async route is introduced in Task 6 so later pipelined bytes are not dispatched ahead of an unfinished request.

- [ ] **Step 6: Run dispatcher and protocol tests**

Run:

~~~bash
cmake --build build --target http_dispatcher_test http_protocol_test router_test middleware_test -j2
ctest --test-dir build -R 'http_dispatcher_test|http_protocol_test|router_test|middleware_test' --output-on-failure
~~~

Expected: four tests pass.

- [ ] **Step 7: Commit**

~~~bash
git add CMakeLists.txt src/CMakeLists.txt include/http include/net/TcpConnection.h src/http tests/unit/http_dispatcher_test.cpp
git commit -m "feat: integrate routing and middleware into HttpServer"
~~~

### Task 5: Generic Session Core

**Files:**
- Create: include/session/Session.h
- Create: src/session/Session.cpp
- Create: include/session/SessionStorage.h
- Create: src/session/SessionStorage.cpp
- Create: include/session/SessionManager.h
- Create: src/session/SessionManager.cpp
- Create: tests/unit/session_manager_test.cpp
- Modify: CMakeLists.txt
- Modify: src/CMakeLists.txt

**Interfaces:**
- Consumes: HttpRequest query parameters, Cookie, Authorization header, and system secure randomness.
- Produces: http::session::Session, SessionStorage, MemorySessionStorage, SessionManager, and SessionValidator.

- [ ] **Step 1: Write failing Session tests**

Cover create/load/refresh/destroy, expiration, data values, concurrent MemorySessionStorage save/load, and token extraction priority. The extraction order is Authorization Bearer, Cookie sessionId, then query token.

Use:

~~~cpp
std::unique_ptr<http::session::SessionStorage> storage(
    new http::session::MemorySessionStorage());
http::session::SessionManager manager(std::move(storage), 1800);
http::session::Session session = manager.create();
CHECK(session.id().size() == 64);
CHECK(manager.load(session.id(), &session));
CHECK(manager.destroy(session.id()));
~~~

Run eight threads, each saving and loading 500 unique sessions, then join and verify every operation succeeded.

- [ ] **Step 2: Run the test and verify Session headers are absent**

Run:

~~~bash
cmake --build build --target session_manager_test -j2
~~~

Expected: compilation fails because include/session/SessionManager.h does not exist.

- [ ] **Step 3: Port the Session value object under a non-conflicting namespace**

Use namespace http::session so it does not conflict with the existing global AR Session in include/db/SessionDAO.h. Session owns id, unordered_map<string,string>, expiresAt milliseconds, and ttlSeconds. setValue(), value(), remove(), clear(), refresh(nowMs), and expired(nowMs) are value operations and do not call storage recursively.

- [ ] **Step 4: Implement thread-safe storage**

Define:

~~~cpp
class SessionStorage {
public:
    virtual ~SessionStorage() {}
    virtual bool save(const Session&) = 0;
    virtual bool load(const std::string&, Session*) = 0;
    virtual bool remove(const std::string&) = 0;
};
~~~

MemorySessionStorage guards its unordered_map with one mutex. load removes expired entries before returning false. Never return a shared mutable Session stored inside the map; copy values across the interface.

- [ ] **Step 5: Implement SessionManager and validator boundary**

Read 32 bytes from getrandom() on Linux, retry EINTR, and encode them as 64 lowercase hex characters. If getrandom fails, return an empty ID and make create() fail explicitly instead of using rand or mt19937.

Define SessionValidator:

~~~cpp
class SessionValidator {
public:
    virtual ~SessionValidator() {}
    virtual void validate(const std::string& token,
                          const std::function<void(bool)>& completion) = 0;
};
~~~

SessionManager implements SessionValidator synchronously for MemorySessionStorage and exposes static extractToken(const HttpRequest&).

- [ ] **Step 6: Run Session tests under TSan**

Run:

~~~bash
cmake --build build --target session_manager_test -j2
ctest --test-dir build -R '^session_manager_test$' --output-on-failure
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='-fsanitize=thread -fno-omit-frame-pointer'
cmake --build build-tsan --target session_manager_test -j2
./bin/session_manager_test
~~~

Expected: functional test passes and TSan prints no data-race report.

- [ ] **Step 7: Commit**

~~~bash
git add CMakeLists.txt src/CMakeLists.txt include/session src/session tests/unit/session_manager_test.cpp
git commit -m "feat: add thread-safe generic session management"
~~~

### Task 6: Exactly-Once Async Responses and Safe DB Callbacks

**Files:**
- Create: include/http/AsyncResponder.h
- Create: src/http/AsyncResponder.cpp
- Create: tests/unit/async_responder_test.cpp
- Modify: include/router/Router.h
- Modify: src/router/Router.cpp
- Modify: include/http/HttpDispatcher.h
- Modify: src/http/HttpDispatcher.cpp
- Modify: include/http/HttpServer.h
- Modify: src/http/HttpServer.cpp
- Modify: include/db/DBWorkerPool.h
- Modify: src/db/DBWorkerPool.cpp
- Modify: include/db/SessionDAO.h
- Modify: src/db/SessionDAO.cpp
- Modify: CMakeLists.txt
- Modify: src/CMakeLists.txt

**Interfaces:**
- Consumes: EventLoop::queueInLoop(), weak_ptr<TcpConnection>, Router async callback slot, and DBWorkerPool.
- Produces: AsyncResponder::send(), HttpServer::PostAsync/GetAsync, safe DAO shared_ptr callbacks, and no EventLoop waits.

- [ ] **Step 1: Write failing exactly-once tests**

Create an AsyncResponder around a sender lambda that increments an atomic count and captures the response body. Call send() concurrently from eight threads.

~~~cpp
CHECK(responder.send(firstResponse));
CHECK(!responder.send(secondResponse));
CHECK(sendCount.load() == 1);
CHECK(sentBody == "first");
~~~

Add a dispatcher test where an async handler stores its responder, dispatch returns kAsyncPending, and after middleware does not run until responder.send() is called. Then assert after middleware runs exactly once.

- [ ] **Step 2: Run the test and verify AsyncResponder is incomplete**

Run:

~~~bash
cmake --build build --target async_responder_test -j2
~~~

Expected: compilation fails because include/http/AsyncResponder.h does not exist.

- [ ] **Step 3: Implement the generic exactly-once handle**

Define:

~~~cpp
class AsyncResponder {
public:
    typedef std::function<void(HttpResponse)> Sender;
    AsyncResponder();
    explicit AsyncResponder(const Sender& sender);
    bool send(const HttpResponse& response) const;
    bool valid() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
~~~

State contains Sender and atomic_bool completed. send() returns false if invalid or if completed.exchange(true) was already true.

- [ ] **Step 4: Integrate async dispatch with middleware completion**

Forward-declare AsyncResponder in Router.h. addAsync stores AsyncHandlerCallback. HttpDispatcher receives a completion factory from HttpServer. For async routes it captures a copy of HttpRequest and the executed middleware vector. The AsyncResponder sender first queues a closure on the original EventLoop; that closure runs processAfter exactly once and then serializes the response. No middleware after() method runs on a DB or cache worker thread.

Add GetAsync/PostAsync and setAsyncFallback methods to HttpServer. HttpServer's sender captures weak_ptr<TcpConnection>, EventLoop pointer, and close policy. It calls loop->queueInLoop(), locks the weak pointer, serializes the response, and sends only when the connection remains connected.

- [ ] **Step 5: Remove unsafe DB request behavior**

Change DBWorkerPool::submit to return bool and always execute an accepted DBTask exactly once, passing an empty shared_ptr<MYSQL> when borrowing fails or shutdown cancels the task. Change run() to drain accepted tasks during shutdown. Repair runSync by capturing shared_ptr<promise<R>> by value; retain it for tests only.

Change SessionDAO callback signatures to:

~~~cpp
typedef std::function<void(const std::shared_ptr<User>&)> UserCallback;
typedef std::function<void(const std::shared_ptr<Session>&)> SessionCallback;
typedef std::function<void(bool)> BoolCallback;
typedef std::function<void(uint64_t)> IdCallback;
~~~

Every DAO lambda checks conn before mysql_stmt_init, returns an error through its callback, and never passes a raw pointer that is deleted immediately after callback return.

- [ ] **Step 6: Verify no EventLoop path uses runSync**

Run:

~~~bash
rg -n 'runSync|future::get|future::wait_for' src WebApps include
~~~

Expected after Task 9 migration: only the compatibility template and its comments remain. At this task, src/main.cpp matches are allowed temporarily and are listed for removal in Task 9; no new framework file may contain a match.

- [ ] **Step 7: Run framework tests**

Run:

~~~bash
cmake --build build --target async_responder_test http_dispatcher_test router_test -j2
ctest --test-dir build -R 'async_responder_test|http_dispatcher_test|router_test' --output-on-failure
~~~

Expected: three tests pass.

- [ ] **Step 8: Commit**

~~~bash
git add CMakeLists.txt src/CMakeLists.txt include/http include/router include/db src/http src/router src/db tests/unit/async_responder_test.cpp
git commit -m "feat: add safe asynchronous HTTP responses"
~~~

### Task 7: Bounded Cache Workers and Redis Session Adapter

**Files:**
- Create: include/base/TaskWorkerPool.h
- Create: src/base/TaskWorkerPool.cpp
- Create: include/session/RedisSessionStorage.h
- Create: src/session/RedisSessionStorage.cpp
- Create: tests/unit/task_worker_pool_test.cpp
- Create: tests/unit/session_codec_test.cpp
- Create: tests/integration/redis_session_integration.sh
- Modify: include/cache/RedisConnectionPool.h
- Modify: src/cache/RedisConnectionPool.cpp
- Modify: CMakeLists.txt
- Modify: src/CMakeLists.txt

**Interfaces:**
- Consumes: RedisConnectionPool, http::session::Session, and SessionStorage.
- Produces: TaskWorkerPool::submit(), bounded pendingCount(), and RedisSessionStorage ready for extraction into http_framework_redis in Task 9.

- [ ] **Step 1: Write failing bounded-pool and codec tests**

TaskWorkerPool test uses one worker and capacity one. Block the first task on a condition variable, enqueue the second task, and assert a third submit returns false. Release the condition, wait for both accepted tasks, and assert each ran once.

Session codec test creates values containing pipe, newline, NUL, and UTF-8 bytes, serializes and deserializes them, and asserts byte equality. Use:

~~~cpp
std::string encoded = http::session::RedisSessionStorage::encode(session);
http::session::Session decoded;
CHECK(http::session::RedisSessionStorage::decode(encoded, &decoded));
CHECK(decoded.value("binary") == binaryValue);
~~~

- [ ] **Step 2: Run tests and verify the worker and adapter are absent**

Run:

~~~bash
cmake --build build --target task_worker_pool_test session_codec_test -j2
~~~

Expected: compilation fails because TaskWorkerPool and RedisSessionStorage do not exist.

- [ ] **Step 3: Implement the bounded worker pool**

Define:

~~~cpp
class TaskWorkerPool : noncopyable {
public:
    typedef std::function<void()> Task;
    TaskWorkerPool(size_t workers, size_t capacity);
    ~TaskWorkerPool();
    bool submit(const Task& task);
    size_t pendingCount() const;
};
~~~

submit returns false when stopped or when queue size equals capacity. Destruction stops acceptance, drains accepted tasks, notifies all workers, and joins them. No task executes while the queue mutex is held.

- [ ] **Step 4: Implement binary-safe Session serialization**

Encode Session fields with unsigned 32-bit network-order length prefixes and fixed-width 64-bit expiry. Validate every length against the remaining buffer during decode, reject duplicate data keys, and reject trailing bytes. Do not use delimiter-separated text.

RedisSessionStorage uses SETEX on http_session:{token}, GET, and DEL. It receives RedisConnectionPool* and returns false on missing pool, borrow timeout, Redis error, or decode error. No Redis command is called from an EventLoop; callers submit storage operations to TaskWorkerPool.

- [ ] **Step 5: Add the optional Redis target**

Keep the compatibility src_lib target through this task. When HIREDIS_LIB is present, add RedisSessionStorage.cpp to src_lib beside the existing Redis sources; when hiredis is absent, exclude it. Task 9 extracts these sources into the final http_framework_redis target. Add the two unit tests unconditionally, testing only the binary codec when hiredis is absent.

Create redis_session_integration.sh to flush only keys matching http_session:integration-*, save/load/remove one session through the test executable, and verify TTL is between 1790 and 1800. Register it only when ENABLE_REDIS_INTEGRATION_TESTS=ON.

- [ ] **Step 6: Run unit tests**

Run:

~~~bash
cmake --build build --target task_worker_pool_test session_codec_test -j2
ctest --test-dir build -R 'task_worker_pool_test|session_codec_test' --output-on-failure
~~~

Expected: two tests pass.

- [ ] **Step 7: Commit**

~~~bash
git add CMakeLists.txt src/CMakeLists.txt include/base include/session include/cache src/base src/session src/cache tests/unit tests/integration/redis_session_integration.sh
git commit -m "feat: add bounded cache workers and Redis sessions"
~~~

### Task 8: Reusable Static File Handler and Safe sendfile

**Files:**
- Create: include/http/StaticFileHandler.h
- Create: src/http/StaticFileHandler.cpp
- Create: tests/unit/static_file_handler_test.cpp
- Create: tests/integration/static_file_server_test.sh
- Modify: include/http/HttpResponse.h
- Modify: src/http/HttpResponse.cpp
- Modify: include/http/StaticFileCache.h
- Modify: include/http/HttpServer.h
- Modify: src/http/HttpServer.cpp
- Modify: include/net/TcpConnection.h
- Modify: src/net/TcpConnection.cpp
- Modify: CMakeLists.txt
- Modify: src/CMakeLists.txt

**Interfaces:**
- Consumes: cache get/put callbacks backed by StaticFileCache or TwoLevelCache, TaskWorkerPool, AsyncResponder, and TcpConnection::sendFile.
- Produces: StaticFileHandler::handle(), HttpResponse file metadata, AR MIME mappings, conditional requests, and owned file transfer lifecycle.

- [ ] **Step 1: Write failing static file tests**

Create a temporary root with index.html, asset.glb, marker.patt, and an external sibling file. Assert:

- / maps to index.html.
- .glb is model/gltf-binary and .patt is text/plain.
- ../ and percent-decoded traversal are 400.
- an If-None-Match hit is 304 with an empty body.
- a small file returns an in-memory body.
- a file larger than the configured 1 MiB threshold produces file metadata instead of copying bytes into body.
- HEAD has the GET Content-Length but no body.

Use a temporary directory created by mkdtemp() and remove only the files created by the test.

- [ ] **Step 2: Run the test and verify StaticFileHandler is absent**

Run:

~~~bash
cmake --build build --target static_file_handler_test -j2
~~~

Expected: compilation fails because include/http/StaticFileHandler.h does not exist.

- [ ] **Step 3: Add response file metadata**

Add 304 and 416 status values. Add:

~~~cpp
void setFile(const std::string& path, off_t offset, size_t count);
bool hasFile() const;
const std::string& filePath() const;
off_t fileOffset() const;
size_t fileCount() const;
~~~

appendToBuffer writes headers only for file responses and uses fileCount as Content-Length. HttpServer opens the normalized path immediately before transfer; open failure becomes a 404 before any header is sent.

- [ ] **Step 4: Implement StaticFileHandler**

Define cache callbacks and constructor:

~~~cpp
typedef std::function<bool(const std::string&, CachedFileEntry&)> CacheGet;
typedef std::function<void(const std::string&, const CachedFileEntry&)> CachePut;
StaticFileHandler(const std::string& root, const CacheGet& get,
                  const CachePut& put, TaskWorkerPool* workers,
                  size_t largeFileThreshold);
~~~

Resolve the root once with realpath. Decode and normalize each URL path, resolve its parent with realpath, and ensure the result starts with root plus a path separator. Reject directories and non-regular files.

Add MIME mappings for .patt, .glb, .gltf, and .bin. Generate ETag from inode, size, and mtime. Honor If-None-Match and If-Modified-Since. Use LFU/TwoLevelCache only below the large-file threshold. Execute stat, Redis cache, and disk reads on TaskWorkerPool and finish with AsyncResponder.

- [ ] **Step 5: Repair sendfile ownership and partial writes**

Change TcpConnection::sendFile(int fd, off_t offset, size_t count) to take ownership of fd. Store one PendingFile member containing fd, offset, and remaining bytes. Queue the file only after buffered response headers drain. On EPOLLOUT call sendfile once, update offset/remaining, leave EPOLLOUT enabled for EAGAIN, and close fd on completion, error, or connection destruction. Never queue sendFileInLoop recursively.

Add assertions in the integration script that downloading a 4 MiB file twice matches sha256sum and that the server's /proc/{pid}/fd count returns to its pre-download range.

- [ ] **Step 6: Run static tests**

Run:

~~~bash
cmake --build build --target static_file_handler_test -j2
ctest --test-dir build -R '^static_file_handler_test$' --output-on-failure
~~~

Expected: one unit test passes. Run static_file_server_test.sh after ar_server exists in Task 9.

- [ ] **Step 7: Commit**

~~~bash
git add CMakeLists.txt src/CMakeLists.txt include/http include/net src/http src/net tests/unit/static_file_handler_test.cpp tests/integration/static_file_server_test.sh
git commit -m "feat: add safe cached static file serving"
~~~

### Task 9: Move the Existing Application into WebApps/ARServer

**Files:**
- Create: WebApps/ARServer/include/ARServer.h
- Create: WebApps/ARServer/src/ARServer.cpp
- Create: WebApps/ARServer/include/services/AuthService.h
- Create: WebApps/ARServer/src/services/AuthService.cpp
- Create: WebApps/ARServer/include/services/SessionService.h
- Create: WebApps/ARServer/src/services/SessionService.cpp
- Create: WebApps/ARServer/include/services/ArSessionValidator.h
- Create: WebApps/ARServer/src/services/ArSessionValidator.cpp
- Create: WebApps/ARServer/include/handlers/AuthHandler.h
- Create: WebApps/ARServer/src/handlers/AuthHandler.cpp
- Create: WebApps/ARServer/include/handlers/SessionHandlers.h
- Create: WebApps/ARServer/src/handlers/SessionHandlers.cpp
- Create: WebApps/ARServer/include/utils/JsonUtil.h
- Create: WebApps/ARServer/src/utils/JsonUtil.cpp
- Create: WebApps/ARServer/src/main.cpp
- Create: WebApps/ARServer/CMakeLists.txt
- Create: tests/unit/ar_handlers_test.cpp
- Move: www to WebApps/ARServer/www without content edits
- Delete after migration: src/main.cpp
- Modify: CMakeLists.txt
- Modify: src/CMakeLists.txt

**Interfaces:**
- Consumes: HttpServer, AsyncResponder, SessionManager, SessionDAO, SessionCache, StaticFileHandler, MySQLConnectionPool, RedisConnectionPool, and TaskWorkerPool.
- Produces: ar_server executable and compatible /api/auth, /api/session/enter, /api/session/exit, and /api/session endpoints.

- [ ] **Step 1: Record frontend checksums and write failing handler tests**

Run before moving:

~~~bash
sha256sum www/index.html www/css/style.css www/js/app.js > /tmp/ar-frontend-before.sha256
~~~

Create handler tests with fake service interfaces. Assert missing auth fields return 400 JSON; successful auth preserves status, is_new, username, user_id, and session_token; invalid password returns 401; enter returns scene_id; repeat exit returns the existing already exited error; query returns all existing Session fields. Assert JsonUtil escapes quote, backslash, control characters, and UTF-8 correctly.

- [ ] **Step 2: Run the test and verify AR handlers are absent**

Run:

~~~bash
cmake --build build --target ar_handlers_test -j2
~~~

Expected: compilation fails because WebApps/ARServer handlers do not exist.

- [ ] **Step 3: Implement service interfaces and async handlers**

AuthService::authenticate(username, password, completion) uses SessionDAO prepared-statement methods only. Existing users must have matching passwdHash; mismatch returns unauthorized. New users are inserted, SessionManager creates a secure token, SessionDAO creates the durable session, and SessionCache is populated best-effort. On durable session failure, destroy the framework Session.

ArSessionValidator implements http::session::SessionValidator without blocking its caller. It checks the framework Session and SessionCache on TaskWorkerPool, then falls back to SessionDAO. It invokes completion exactly once and repopulates both Redis namespaces after a successful MySQL lookup.

SessionService exposes:

~~~cpp
void get(const std::string& token, const SessionCallback& completion);
void enter(const std::string& token, const std::string& sceneId,
           const BoolCallback& completion);
void exit(const std::string& token, const ExitCallback& completion);
~~~

get checks SessionCache on TaskWorkerPool and falls back asynchronously to SessionDAO. Change SessionDAO::findSessionByToken to return the latest row regardless of status so the compatibility query can report an exited Session; ArSessionValidator separately requires status=1 where appropriate. updateSessionScene sets scene_id and status=1 in one prepared statement, and endSession sets status=0 and scene_id='' in one prepared statement. enter and exit update MySQL first, then update or remove the Redis cache. Handlers validate query values on the EventLoop and send through AsyncResponder from service callbacks.

- [ ] **Step 4: Compose ARServer without exposing network internals**

ARServer owns or receives all pools/services, registers the four API routes with PostAsync/GetAsync, installs StaticFileHandler as the non-API fallback, and never includes Channel.h, Socket.h, or TcpConnection.h. main.cpp only loads configuration, starts logging/memory pool, constructs dependencies, starts ARServer, and enters EventLoop.

- [ ] **Step 5: Move frontend bytes and update CMake**

Use:

~~~bash
mkdir -p WebApps/ARServer
git mv www WebApps/ARServer/www
sha256sum WebApps/ARServer/www/index.html WebApps/ARServer/www/css/style.css WebApps/ARServer/www/js/app.js > /tmp/ar-frontend-after.sha256
sed 's#WebApps/ARServer/##' /tmp/ar-frontend-after.sha256 > /tmp/ar-frontend-after-normalized.sha256
diff -u /tmp/ar-frontend-before.sha256 /tmp/ar-frontend-after-normalized.sha256
~~~

Expected: diff prints no output. Create targets with non-overlapping source ownership:

- http_framework: base, net, timer, log, http, router, middleware, generic Session, and a link to memory_lib.
- http_framework_redis: RedisConnectionPool plus RedisSessionStorage; link http_framework and hiredis.
- ar_data: MySQLConnectionPool, DBWorkerPool, SessionDAO, and, when Redis is enabled, SessionCache and TwoLevelCache; link http_framework_redis instead of compiling RedisConnectionPool again.
- ar_server: WebApps/ARServer sources linked to http_framework and ar_data.

Remove src_lib and the old main target only after ar_server links successfully.

- [ ] **Step 6: Prove blocking calls left the EventLoop path**

Run:

~~~bash
rg -n 'runSync|future::get|future::wait_for' src WebApps include
rg -n '#include.*(Channel|Socket|TcpConnection)' WebApps/ARServer
~~~

Expected: runSync appears only in its compatibility definition/comments; the second command prints no output.

- [ ] **Step 7: Run compatibility tests**

Run:

~~~bash
cmake --build build --target ar_server ar_handlers_test -j2
ctest --test-dir build -R 'ar_handlers_test|session_cache_test' --output-on-failure
tests/integration/static_file_server_test.sh
~~~

Expected: handler and cache tests pass, current index page is served, and static file checks pass.

- [ ] **Step 8: Commit**

~~~bash
git add CMakeLists.txt src/CMakeLists.txt include src WebApps tests/unit/ar_handlers_test.cpp
git commit -m "refactor: build ARServer on the HTTP framework"
~~~

### Task 10: Presence, Scene Metadata, and Weak Collaboration APIs

**Files:**
- Create: WebApps/ARServer/include/services/PresenceService.h
- Create: WebApps/ARServer/src/services/PresenceService.cpp
- Create: WebApps/ARServer/include/handlers/SceneHandlers.h
- Create: WebApps/ARServer/src/handlers/SceneHandlers.cpp
- Create: tests/unit/presence_service_test.cpp
- Create: tests/integration/ar_collaboration_test.sh
- Modify: WebApps/ARServer/include/services/SessionService.h
- Modify: WebApps/ARServer/src/services/SessionService.cpp
- Modify: WebApps/ARServer/include/handlers/SessionHandlers.h
- Modify: WebApps/ARServer/src/handlers/SessionHandlers.cpp
- Modify: WebApps/ARServer/src/ARServer.cpp
- Modify: WebApps/ARServer/CMakeLists.txt

**Interfaces:**
- Consumes: RedisConnectionPool, SessionCache, TaskWorkerPool, AsyncResponder, and dynamic sceneId route parameters.
- Produces: PresenceService::heartbeat/list/remove, scene metadata routes, heartbeat route, members route, and the fixed 501 interaction route.

- [ ] **Step 1: Write failing PresenceService tests against a fake Redis port**

Extract Redis commands behind a small PresenceStore interface:

~~~cpp
class PresenceStore {
public:
    virtual ~PresenceStore() {}
    virtual bool touch(const std::string& sceneId, const std::string& token,
                       int64_t nowMs) = 0;
    virtual bool remove(const std::string& sceneId,
                        const std::string& token) = 0;
    virtual bool active(const std::string& sceneId, int64_t cutoffMs,
                        std::vector<PresenceEntry>* entries) = 0;
};
~~~

Use a fake store and fake Session lookup to assert heartbeat rejects a token not in the requested active scene, active() uses now minus 30000, expired members are omitted, returned member JSON includes member_id, user_id, and last_seen_ms but never includes session_token, and exit removes presence.

- [ ] **Step 2: Run the test and verify PresenceService is absent**

Run:

~~~bash
cmake --build build --target presence_service_test -j2
~~~

Expected: compilation fails because PresenceService.h does not exist.

- [ ] **Step 3: Implement the Redis presence store**

Use key scene:{sceneId}:presence. On the TaskWorkerPool instance named cacheWorkerPool execute:

~~~text
ZADD scene:{sceneId}:presence {nowMs} {token}
ZREM scene:{sceneId}:presence {token}
ZREMRANGEBYSCORE scene:{sceneId}:presence -inf {cutoffMs}
ZRANGEBYSCORE scene:{sceneId}:presence {cutoffMs} +inf WITHSCORES
~~~

Parse every score with checked integer conversion. Treat Redis errors, borrow timeout, and a full worker queue as service unavailable. list() resolves each token through SessionCache, filters status=1 and matching sceneId, and exposes Session.id as member_id instead of returning the token.

- [ ] **Step 4: Add API handlers and scene metadata**

Register:

~~~cpp
server.PostAsync("/api/session/heartbeat", heartbeatHandler);
server.GetAsync("/api/scenes/:sceneId/members", membersHandler);
server.Get("/api/scenes", listScenesHandler);
server.Get("/api/scenes/:sceneId", getSceneHandler);
server.Post("/api/scenes/:sceneId/interactions", interactionHandler);
~~~

The five scene records use the existing IDs and Chinese names from www/js/app.js. Marker and model URLs are deterministic under /assets/scenes/{id}/ but may point to files not present until AR.js work begins. interactions always returns HTTP 501 with code INTERACTIONS_NOT_IMPLEMENTED.

- [ ] **Step 5: Connect presence to enter and exit**

After MySQL enter succeeds, call PresenceService::heartbeat with the current timestamp; if Redis fails, keep the durable enter successful but return a warning field presence_available:false. On exit, update MySQL first and then best-effort ZREM. Heartbeat itself returns 503 when Redis is unavailable.

- [ ] **Step 6: Run unit and two-user integration tests**

The integration script creates two unique users, enters both into scene 1, heartbeats both, queries members and checks two distinct user IDs, exits one, checks one member, stops heartbeating the other, waits 31 seconds, and checks zero members.

Run:

~~~bash
cmake --build build --target presence_service_test ar_server -j2
ctest --test-dir build -R '^presence_service_test$' --output-on-failure
tests/integration/ar_collaboration_test.sh
~~~

Expected: the unit test passes and the script prints PASS: weak collaboration lifecycle.

- [ ] **Step 7: Commit**

~~~bash
git add WebApps/ARServer tests/unit/presence_service_test.cpp tests/integration/ar_collaboration_test.sh
git commit -m "feat: add AR scene presence and polling APIs"
~~~

### Task 11: Request Middleware, External Configuration, and Error Contract

**Files:**
- Create: include/middleware/RequestIdMiddleware.h
- Create: src/middleware/RequestIdMiddleware.cpp
- Create: include/middleware/AccessLogMiddleware.h
- Create: src/middleware/AccessLogMiddleware.cpp
- Create: WebApps/ARServer/include/middleware/AuthMiddleware.h
- Create: WebApps/ARServer/src/middleware/AuthMiddleware.cpp
- Create: WebApps/ARServer/include/config/AppConfig.h
- Create: WebApps/ARServer/src/config/AppConfig.cpp
- Create: WebApps/ARServer/include/utils/ApiError.h
- Create: WebApps/ARServer/src/utils/ApiError.cpp
- Create: WebApps/ARServer/.env.example
- Create: tests/unit/app_config_test.cpp
- Create: tests/unit/request_middleware_test.cpp
- Modify: WebApps/ARServer/src/ARServer.cpp
- Modify: WebApps/ARServer/src/main.cpp
- Modify: WebApps/ARServer/CMakeLists.txt
- Modify: README.md

**Interfaces:**
- Consumes: HttpRequest attributes, Logger output, SessionManager::extractToken(), and AR service validation.
- Produces: request_id propagation, password-safe access logs, token extraction, AppConfig::fromEnvironment(), and makeApiError().

- [ ] **Step 1: Write failing configuration and middleware tests**

AppConfig test supplies a map-based EnvironmentReader and asserts defaults for host 0.0.0.0, port 8080, threads 3, cache capacity 200, DB workers 4, cache workers 4, body limit 1048576, and static root WebApps/ARServer/www. Assert missing MYSQL_PASSWORD is an error when MySQL mode is enabled.

Middleware test asserts RequestIdMiddleware adds a non-empty request_id attribute and X-Request-Id response header, eight parallel requests receive distinct IDs, and:

~~~cpp
CHECK(AccessLogMiddleware::sanitizeTarget(
    "/api/auth?username=alice&password=secret")
    == "/api/auth?username=alice&password=%5BREDACTED%5D");
~~~

Assert AuthMiddleware returns 401 for a protected route without a token, stores an extracted token as request attribute auth.token, and does not perform Redis or MySQL I/O.

- [ ] **Step 2: Run tests and verify new middleware/configuration is absent**

Run:

~~~bash
cmake --build build --target app_config_test request_middleware_test -j2
~~~

Expected: compilation fails because AppConfig and RequestIdMiddleware do not exist.

- [ ] **Step 3: Implement deterministic configuration**

AppConfig reads AR_HOST, AR_PORT, AR_THREADS, AR_STATIC_ROOT, AR_CACHE_CAPACITY, MYSQL_HOST, MYSQL_PORT, MYSQL_USER, MYSQL_PASSWORD, MYSQL_DATABASE, MYSQL_POOL_SIZE, REDIS_HOST, REDIS_PORT, REDIS_POOL_SIZE, DB_WORKERS, CACHE_WORKERS, SESSION_TTL_SECONDS, and MAX_BODY_BYTES. Parse unsigned integers with full-string validation and range checks. Return a vector of explicit errors; main prints them and exits non-zero before opening sockets.

.env.example contains names and safe local defaults but uses MYSQL_PASSWORD=change-me, never a real credential.

- [ ] **Step 4: Implement request and auth middleware**

RequestIdMiddleware combines a process-random 64-bit prefix with an atomic 64-bit counter and stores lowercase hex in request attributes. AccessLogMiddleware records monotonic start microseconds in an attribute, emits request_id, method, sanitized target, status, and latency from after(), and redacts password case-insensitively.

AuthMiddleware is intentionally synchronous: it only decides whether a route is public, extracts the token, rejects a missing token, and stores auth.token. It does not claim the token is valid. Each async AR service validates through ArSessionValidator, preserving the non-blocking MySQL fallback.

- [ ] **Step 5: Standardize API errors**

Define:

~~~cpp
HttpResponse makeApiError(HttpResponse::HttpStatusCode status,
                          const std::string& code,
                          const std::string& message,
                          const std::string& requestId);
~~~

Replace handler-specific error strings with codes BAD_REQUEST, UNAUTHORIZED, FORBIDDEN, NOT_FOUND, METHOD_NOT_ALLOWED, SESSION_CONFLICT, PAYLOAD_TOO_LARGE, INTERNAL_ERROR, INTERACTIONS_NOT_IMPLEMENTED, and SERVICE_UNAVAILABLE while retaining the current status/error fields expected by www/js/app.js.

- [ ] **Step 6: Remove hard-coded credentials and update startup docs**

Delete the literal MySQL password and fixed host/port values from application code. Construct all pools from AppConfig. Document one explicit startup command using exported environment variables and warn that query-string password support exists only for the unchanged compatibility page.

Run:

~~~bash
rg -n 'Wsy258258|passwd.*=.*"' --glob '!Kama-HTTPServer/**' .
~~~

Expected: no secret match in project source or documentation.

- [ ] **Step 7: Run tests**

Run:

~~~bash
cmake --build build --target app_config_test request_middleware_test ar_handlers_test -j2
ctest --test-dir build -R 'app_config_test|request_middleware_test|ar_handlers_test' --output-on-failure
~~~

Expected: three tests pass.

- [ ] **Step 8: Commit**

~~~bash
git add include/middleware src/middleware WebApps/ARServer README.md tests/unit/app_config_test.cpp tests/unit/request_middleware_test.cpp
git commit -m "feat: externalize configuration and standardize requests"
~~~

### Task 12: Installable CMake Package and Consumer Proof

**Files:**
- Create: cmake/http_frameworkConfig.cmake.in
- Create: tests/consumer/CMakeLists.txt
- Create: tests/consumer/main.cpp
- Create: tests/integration/consumer_build_test.sh
- Modify: CMakeLists.txt
- Modify: src/CMakeLists.txt
- Modify: memory/CMakeLists.txt
- Modify: WebApps/ARServer/CMakeLists.txt
- Modify: README.md

**Interfaces:**
- Consumes: http_framework public headers and library targets.
- Produces: http_framework::http_framework CMake package, optional http_framework::redis target, and a clean external consumer build.

- [ ] **Step 1: Write the failing external consumer**

tests/consumer/main.cpp only includes HttpServer, Router-facing registration APIs, and EventLoop. It registers GET /health returning plain text ok. Its CMakeLists uses:

~~~cmake
find_package(http_framework CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE http_framework::http_framework)
~~~

consumer_build_test.sh installs into a mktemp directory, configures the consumer with CMAKE_PREFIX_PATH, builds it, launches it on an unused port, curls /health, and checks the body equals ok.

- [ ] **Step 2: Run the consumer test and verify package config is absent**

Run:

~~~bash
tests/integration/consumer_build_test.sh
~~~

Expected: CMake fails to find http_frameworkConfig.cmake.

- [ ] **Step 3: Replace global build settings with target-scoped settings**

Set CXX_STANDARD 11 and include directories on targets. Add HTTP_FRAMEWORK_WITH_MYSQL and HTTP_FRAMEWORK_WITH_REDIS cache strings accepting AUTO, ON, or OFF; default both to AUTO, fail configuration when ON cannot find the dependency, and disable quietly when AUTO cannot find it. Export http_framework with namespace http_framework::. Install public headers preserving subdirectories. Generate and install http_frameworkConfig.cmake plus version metadata. Keep MySQL and hiredis optional and do not leak their include directories into a consumer of the core target.

Provide BUILD_SHARED_LIBS support rather than building both variants in one build. Export the Redis adapter only when hiredis is found. Set runtime, library, and archive outputs under CMAKE_BINARY_DIR so ASan, TSan, Debug, and Release builds cannot overwrite one another's artifacts.

- [ ] **Step 4: Run clean core-only and full builds**

Run:

~~~bash
cmake -S . -B build-core -DHTTP_FRAMEWORK_WITH_MYSQL=OFF -DHTTP_FRAMEWORK_WITH_REDIS=OFF
cmake --build build-core --target http_framework -j2
cmake -S . -B build-full -DCMAKE_BUILD_TYPE=Release
cmake --build build-full --target ar_server -j2
tests/integration/consumer_build_test.sh
~~~

Expected: core builds without MySQL/hiredis, full ARServer builds with detected dependencies, and consumer script prints PASS: external consumer.

- [ ] **Step 5: Commit**

~~~bash
git add CMakeLists.txt src/CMakeLists.txt memory/CMakeLists.txt WebApps/ARServer/CMakeLists.txt cmake tests/consumer tests/integration/consumer_build_test.sh README.md
git commit -m "build: package the HTTP framework for external consumers"
~~~

### Task 13: End-to-End, Failure, Sanitizer, and Benchmark Evidence

**Files:**
- Create: tests/integration/ar_api_test.sh
- Create: tests/integration/redis_fallback_test.sh
- Create: tests/integration/disconnected_client_test.sh
- Create: benchmark/framework_overhead.sh
- Create: benchmark/ar_api.sh
- Create: docs/operations/failure-drills.md
- Modify: benchmark/benchmark.md
- Modify: README.md

**Interfaces:**
- Consumes: final ar_server, MySQL, Redis, curl, redis-cli, wrk, ASan, and TSan.
- Produces: reproducible acceptance evidence for every design requirement.

- [ ] **Step 1: Add the end-to-end compatibility script**

ar_api_test.sh starts from BASE_URL, creates a unique user, verifies all response fields consumed by the unchanged app.js, enters each scene ID 1 through 5, queries the Session, exits, verifies repeat exit returns already exited, checks OPTIONS CORS, checks 404/405, and verifies interactions returns 501. Every curl uses --fail-with-body and the script prints only one PASS line on success.

- [ ] **Step 2: Add controlled failure scripts**

redis_fallback_test.sh requires ALLOW_SERVICE_CONTROL=1 before issuing any stop/start command. It authenticates, removes session:{token}, confirms MySQL repopulates it, stops only the configured test Redis service, confirms GET /api/session still succeeds, confirms members returns 503, and restarts Redis in a trap.

disconnected_client_test.sh opens a request to an endpoint configured with a test-only 200 ms DB delay, closes the socket immediately, waits one second, and verifies ar_server remains alive and its log contains no use-after-free or double-send marker.

Document the exact safe preconditions and recovery commands in docs/operations/failure-drills.md.

- [ ] **Step 3: Run complete functional acceptance**

Run:

~~~bash
ctest --test-dir build-full --output-on-failure
tests/integration/ar_api_test.sh
tests/integration/ar_collaboration_test.sh
tests/integration/static_file_server_test.sh
tests/integration/consumer_build_test.sh
~~~

Expected: all CTest tests pass and every script prints PASS.

- [ ] **Step 4: Run sanitizers**

Run:

~~~bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build-asan -j2
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan --output-on-failure
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='-fsanitize=thread -fno-omit-frame-pointer'
cmake --build build-tsan --target session_manager_test task_worker_pool_test async_responder_test -j2
ctest --test-dir build-tsan -R 'session_manager_test|task_worker_pool_test|async_responder_test' --output-on-failure
~~~

Expected: all selected tests pass with no sanitizer report.

- [ ] **Step 5: Capture framework and AR performance**

framework_overhead.sh benchmarks a direct callback, a static route, a dynamic route, and a route with CORS plus access log at concurrency 10, 100, and 500 for 15 seconds after ten warmup requests. ar_api.sh measures cached Session reads and scene-member polling. Record QPS, P50, P90, P99, errors, CPU, thread count, payload size, compiler flags, cache state, and log state in benchmark/benchmark.md.

Do not replace old measurements. Label old and new environments separately and explain that localhost cached-static-file QPS is not a production network capacity claim.

- [ ] **Step 6: Update the README project story**

Document:

- three-minute full build and launch;
- core-only consumer build;
- architecture diagram from Reactor through middleware/router to AR services;
- current page workflow and future AR.js replacement boundary;
- Redis/MySQL degradation table;
- exact tests and benchmark commands;
- which Kama modules were authorized, selected, adapted, and corrected;
- concise resume bullets supported by measured evidence.

- [ ] **Step 7: Commit**

~~~bash
git add tests/integration benchmark README.md docs/operations
git commit -m "test: add reproducible ARServer acceptance evidence"
~~~

## Plan Self-Review

- Spec sections 1 through 4 are covered by the global constraints and Tasks 1 through 3.
- Directory, build, and public API requirements are covered by Tasks 4, 9, and 12.
- Router, middleware, CORS, and request lifecycle requirements are covered by Tasks 1 through 4 and Task 6.
- Generic Session, AR Session, Redis fallback, and non-blocking storage requirements are covered by Tasks 5 through 7 and Task 9.
- Presence, heartbeat, member polling, scene metadata, and the empty interaction endpoint are covered by Task 10.
- Static resources, AR MIME types, caching, conditional requests, and sendfile lifecycle are covered by Task 8.
- Error, configuration, secret removal, and request logging requirements are covered by Task 11.
- Unit, integration, failure, sanitizer, performance, and documentation evidence are covered by Tasks 12 and 13.
- Placeholder scan found no TODO, TBD, FIXME, vague “write tests,” or “similar to another task” instructions.
- Type review uses one global AsyncResponder, http::router for routing, http::middleware for middleware, and http::session for generic Session; the existing global AR Session remains unambiguous.
- Build review assigns every Redis source to one final target and keeps compatibility src_lib only until Task 9.

## Final Verification

- [ ] Run git diff --check and confirm no formatting errors.
- [ ] Run rg -n 'runSync|future::get|future::wait_for' src WebApps include and confirm only the compatibility definition/comments remain.
- [ ] Run rg -n 'Wsy258258|password=.*[^e]$' --glob '!Kama-HTTPServer/**' . and inspect every match for secrets.
- [ ] Run the full CTest, integration, ASan, and selected TSan commands from Task 13.
- [ ] Run git status --short and confirm Kama-HTTPServer and the user's interview-question Markdown remain untouched unless the user separately requests them.
