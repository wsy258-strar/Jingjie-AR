#include "TestSupport.h"

#include <middleware/RequestIdMiddleware.h>
#include <middleware/AccessLogMiddleware.h>
#include <middleware/AuthMiddleware.h>

#include <set>
#include <thread>
#include <vector>

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
    CHECK(AccessLogMiddleware::sanitizeTarget("/api/auth?username=alice&password=secret") ==
          "/api/auth?username=alice&password=%5BREDACTED%5D");
    ar::AuthMiddleware auth;
    HttpRequest protectedRequest;
    protectedRequest.setPath("/api/session");
    HttpResponse protectedResponse(false);
    CHECK(!auth.before(protectedRequest, protectedResponse));
    CHECK(protectedResponse.statusCode() == HttpResponse::k401Unauthorized);
    protectedRequest.setQuery("token=example-token");
    protectedResponse = HttpResponse(false);
    CHECK(auth.before(protectedRequest, protectedResponse));
    CHECK(protectedRequest.attribute("auth.token") == "example-token");
    HttpRequest staticRequest;
    staticRequest.setPath("/index.html");
    HttpResponse staticResponse(false);
    CHECK(auth.before(staticRequest, staticResponse));
    HttpRequest publicComments;
    publicComments.setMethod(HttpRequest::kGet);
    publicComments.setPath("/api/scenes/golden-bay/comments");
    HttpResponse publicCommentsResponse(false);
    CHECK(auth.before(publicComments, publicCommentsResponse));
    HttpRequest protectedLike;
    protectedLike.setMethod(HttpRequest::kPost);
    protectedLike.setPath("/api/scenes/golden-bay/likes");
    HttpResponse protectedLikeResponse(false);
    CHECK(!auth.before(protectedLike, protectedLikeResponse));
    CHECK(protectedLikeResponse.statusCode() == HttpResponse::k401Unauthorized);
    return 0;
}
