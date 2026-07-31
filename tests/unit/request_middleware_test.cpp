#include "TestSupport.h"

#include <middleware/RequestIdMiddleware.h>
#include <middleware/AccessLogMiddleware.h>
#include <middleware/AuthMiddleware.h>

#include <set>
#include <thread>
#include <vector>

namespace {

HttpRequest apiRequest(HttpRequest::Method method, const std::string& path,
                       const std::string& bearer = std::string(),
                       const std::string& visitor = std::string())
{
    HttpRequest request;
    request.setMethod(method);
    request.setPath(path);
    if (!bearer.empty()) request.addHeader("Authorization", "Bearer " + bearer);
    if (!visitor.empty()) request.addHeader("X-Visitor-Token", visitor);
    return request;
}

void expectAllowed(ar::AuthMiddleware* auth, HttpRequest request,
                   const std::string& expectedUserToken = std::string())
{
    HttpResponse response(false);
    CHECK(auth->before(request, response));
    CHECK(request.attribute("auth.token") == expectedUserToken);
}

void expectDenied(ar::AuthMiddleware* auth, HttpRequest request)
{
    HttpResponse response(false);
    CHECK(!auth->before(request, response));
    CHECK(response.statusCode() == HttpResponse::k401Unauthorized);
    CHECK(response.body().find("\"code\":\"UNAUTHORIZED\"") != std::string::npos);
}

} // namespace

int main()
{
    RequestIdMiddleware middleware;
    HttpRequest request;
    HttpResponse response(false);
    CHECK(!request.attribute("request_id").size());
    CHECK(middleware.before(request, response));
    middleware.after(request, response);
    CHECK(!request.attribute("request_id").empty());
    CHECK(response.header("X-Request-Id") == request.attribute("request_id"));
    std::vector<std::string> ids(8);
    std::vector<std::thread> threads;
    for (size_t index = 0; index < ids.size(); ++index)
    {
        threads.push_back(std::thread([&middleware, &ids, index]() {
            HttpRequest parallelRequest;
            HttpResponse parallelResponse(false);
            CHECK(middleware.before(parallelRequest, parallelResponse));
            middleware.after(parallelRequest, parallelResponse);
            ids[index] = parallelResponse.header("X-Request-Id");
        }));
    }
    for (size_t index = 0; index < threads.size(); ++index) threads[index].join();
    CHECK(std::set<std::string>(ids.begin(), ids.end()).size() == ids.size());
    AccessLogMiddleware accessLog;
    CHECK(accessLog.before(request, response));
    CHECK(!request.attribute("access_log_start_us").empty());
    accessLog.after(request, response);
    CHECK(AccessLogMiddleware::sanitizeTarget(
              "/api/auth?username=alice&password=secret&password=second") ==
          "/api/auth");
    CHECK(AccessLogMiddleware::sanitizeTarget(
              "/api/session?token=user-token&visitorToken=visitor-token") ==
          "/api/session");
    CHECK(AccessLogMiddleware::sanitizeTarget("/api/scenes") == "/api/scenes");
    ar::AuthMiddleware auth;
    expectAllowed(&auth, apiRequest(HttpRequest::kPost, "/api/visitors/session"));
    expectAllowed(&auth, apiRequest(HttpRequest::kGet, "/api/scenes"));
    expectAllowed(&auth, apiRequest(HttpRequest::kGet, "/api/scenes/scene-a"));
    expectAllowed(&auth, apiRequest(HttpRequest::kGet, "/api/presence"));
    expectAllowed(&auth, apiRequest(HttpRequest::kGet, "/api/statistics/views"));
    expectAllowed(&auth, apiRequest(HttpRequest::kGet, "/api/artworks/artwork-a"));
    expectAllowed(&auth, apiRequest(HttpRequest::kGet, "/api/artworks/artwork-a/comments"));
    expectAllowed(&auth, apiRequest(HttpRequest::kGet, "/api/artworks/artwork-a", "user-token"),
                  "user-token");
    expectAllowed(&auth, apiRequest(HttpRequest::kGet, "/api/artworks/artwork-a/comments",
                                    "user-token"), "user-token");

    // Visitor 身份只由专用请求头表达，不能授权作品写操作。
    expectAllowed(&auth, apiRequest(HttpRequest::kPost, "/api/presence/heartbeat", std::string(),
                                    "visitor-token"));
    expectAllowed(&auth, apiRequest(HttpRequest::kPost, "/api/presence/exit", std::string(),
                                    "visitor-token"));
    expectDenied(&auth, apiRequest(HttpRequest::kPost, "/api/presence/heartbeat", "user-token"));
    expectDenied(&auth, apiRequest(HttpRequest::kPost, "/api/presence/exit", "user-token"));
    expectDenied(&auth, apiRequest(HttpRequest::kPost, "/api/artworks/artwork-a/likes",
                                   std::string(), "visitor-token"));
    expectDenied(&auth, apiRequest(HttpRequest::kDelete, "/api/artworks/artwork-a/likes",
                                   std::string(), "visitor-token"));
    expectDenied(&auth, apiRequest(HttpRequest::kPost, "/api/artworks/artwork-a/comments",
                                   std::string(), "visitor-token"));
    expectAllowed(&auth, apiRequest(HttpRequest::kPost, "/api/artworks/artwork-a/likes",
                                    "user-token"), "user-token");
    expectAllowed(&auth, apiRequest(HttpRequest::kDelete, "/api/artworks/artwork-a/likes",
                                    "user-token"), "user-token");
    expectAllowed(&auth, apiRequest(HttpRequest::kPost, "/api/artworks/artwork-a/comments",
                                    "user-token"), "user-token");
    HttpRequest staticRequest;
    staticRequest.setPath("/index.html");
    HttpResponse staticResponse(false);
    CHECK(auth.before(staticRequest, staticResponse));
    // 已退役旧写路由不能因为携带任意 Token 被误判为新作品接口。
    expectDenied(&auth, apiRequest(HttpRequest::kPost, "/api/scenes/golden-bay/likes"));
    return 0;
}
