#include "TestSupport.h"

#include <http/HttpRequest.h>
#include <http/HttpResponse.h>
#include <router/Router.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

void healthHandler(const HttpRequest&, HttpResponse* response)
{
    response->setBody("health");
}

void membersHandler(const HttpRequest& request, HttpResponse* response)
{
    response->setBody(request.pathParameter("sceneId"));
}

void dynamicSceneHandler(const HttpRequest&, HttpResponse* response)
{
    response->setBody("dynamic");
}

void exactSceneHandler(const HttpRequest&, HttpResponse* response)
{
    response->setBody("exact");
}

bool containsMethod(const std::vector<HttpRequest::Method>& methods,
                    HttpRequest::Method method)
{
    return std::find(methods.begin(), methods.end(), method) != methods.end();
}

void testDynamicRouteInjectsNamedParameter()
{
    http::router::Router router;
    CHECK(router.add(HttpRequest::kGet, "/health", healthHandler));
    CHECK(router.add(HttpRequest::kGet, "/api/scenes/:sceneId/members", membersHandler));

    HttpRequest request;
    request.setMethod(HttpRequest::kGet);
    request.setPath("/api/scenes/scene-2/members");
    HttpResponse response(false);

    std::vector<HttpRequest::Method> allowedMethods;
    CHECK(router.route(request, &response, &allowedMethods) == http::router::kHandled);
    CHECK(request.pathParameter("sceneId") == "scene-2");
    CHECK(response.body() == "scene-2");
}

void testExactRoutePrecedesDynamicRoute()
{
    http::router::Router router;
    CHECK(router.add(HttpRequest::kGet, "/api/scenes/:sceneId", dynamicSceneHandler));
    CHECK(router.add(HttpRequest::kGet, "/api/scenes/new", exactSceneHandler));

    HttpRequest request;
    request.setMethod(HttpRequest::kGet);
    request.setPath("/api/scenes/new");
    HttpResponse response(false);

    std::vector<HttpRequest::Method> allowedMethods;
    CHECK(router.route(request, &response, &allowedMethods) == http::router::kHandled);
    CHECK(response.body() == "exact");
    CHECK(request.pathParameter("sceneId").empty());
}

void testDuplicateRegistrationIsRejected()
{
    http::router::Router router;
    CHECK(router.add(HttpRequest::kGet, "/health", healthHandler));
    CHECK(!router.add(HttpRequest::kGet, "/health", healthHandler));
}

void testUnknownPathIsNotFound()
{
    http::router::Router router;
    CHECK(router.add(HttpRequest::kGet, "/health", healthHandler));

    HttpRequest request;
    request.setMethod(HttpRequest::kGet);
    request.setPath("/missing");
    HttpResponse response(false);

    std::vector<HttpRequest::Method> allowedMethods;
    CHECK(router.route(request, &response, &allowedMethods) == http::router::kNotFound);
}

void testPostToGetOnlyPathIsMethodNotAllowed()
{
    http::router::Router router;
    CHECK(router.add(HttpRequest::kGet, "/health", healthHandler));

    HttpRequest request;
    request.setMethod(HttpRequest::kPost);
    request.setPath("/health");
    HttpResponse response(false);

    std::vector<HttpRequest::Method> methods;
    CHECK(router.route(request, &response, &methods) == http::router::kMethodNotAllowed);
    CHECK(methods.size() == 1);
    CHECK(methods[0] == HttpRequest::kGet);
}

void testDynamicPathRecordsAllAllowedMethods()
{
    http::router::Router router;
    CHECK(router.add(HttpRequest::kGet, "/api/scenes/:sceneId/members", membersHandler));
    CHECK(router.add(HttpRequest::kPost, "/api/scenes/:id/members", healthHandler));

    HttpRequest request;
    request.setMethod(HttpRequest::kPut);
    request.setPath("/api/scenes/scene-2/members");
    HttpResponse response(false);

    std::vector<HttpRequest::Method> methods;
    CHECK(router.route(request, &response, &methods) == http::router::kMethodNotAllowed);
    CHECK(methods.size() == 2);
    CHECK(containsMethod(methods, HttpRequest::kGet));
    CHECK(containsMethod(methods, HttpRequest::kPost));
}

void testAllowedMethodsAreOwnedByEachRouteCall()
{
    http::router::Router router;
    CHECK(router.add(HttpRequest::kGet, "/health", healthHandler));
    CHECK(router.add(HttpRequest::kPost, "/jobs", healthHandler));

    HttpRequest healthRequest;
    healthRequest.setMethod(HttpRequest::kPost);
    healthRequest.setPath("/health");
    HttpResponse healthResponse(false);
    std::vector<HttpRequest::Method> healthMethods;
    CHECK(router.route(healthRequest, &healthResponse, &healthMethods) ==
          http::router::kMethodNotAllowed);

    HttpRequest jobsRequest;
    jobsRequest.setMethod(HttpRequest::kGet);
    jobsRequest.setPath("/jobs");
    HttpResponse jobsResponse(false);
    std::vector<HttpRequest::Method> jobsMethods;
    CHECK(router.route(jobsRequest, &jobsResponse, &jobsMethods) ==
          http::router::kMethodNotAllowed);

    CHECK(healthMethods.size() == 1);
    CHECK(healthMethods[0] == HttpRequest::kGet);
    CHECK(jobsMethods.size() == 1);
    CHECK(jobsMethods[0] == HttpRequest::kPost);
}

} // namespace

int main()
{
    testDynamicRouteInjectsNamedParameter();
    testExactRoutePrecedesDynamicRoute();
    testDuplicateRegistrationIsRejected();
    testUnknownPathIsNotFound();
    testPostToGetOnlyPathIsMethodNotAllowed();
    testDynamicPathRecordsAllAllowedMethods();
    testAllowedMethodsAreOwnedByEachRouteCall();
    return 0;
}
