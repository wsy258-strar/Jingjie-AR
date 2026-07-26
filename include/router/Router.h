#pragma once

#include <http/HttpRequest.h>
#include <http/HttpResponse.h>

#include <functional>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

class AsyncResponder;

namespace http {
namespace router {

using HandlerCallback = std::function<void(const HttpRequest&, HttpResponse*)>;
using AsyncHandlerCallback =
    std::function<void(const HttpRequest&, const ::AsyncResponder&)>;

enum MatchResult
{
    kHandled,
    kAsyncPending,
    kNotFound,
    kMethodNotAllowed
};

class Router
{
public:
    bool add(HttpRequest::Method method, const std::string& pattern,
             const HandlerCallback& handler);
    bool addAsync(HttpRequest::Method method, const std::string& pattern,
                  const AsyncHandlerCallback& handler);

    MatchResult route(HttpRequest& request, HttpResponse* response,
                      std::vector<HttpRequest::Method>* allowedMethods,
                      const AsyncResponder* responder = 0) const;

private:
    struct Route
    {
        Route(HttpRequest::Method routeMethod, const std::string& routePattern,
              const HandlerCallback& routeHandler);
        Route(HttpRequest::Method routeMethod, const std::string& routePattern,
              const AsyncHandlerCallback& routeHandler);

        HttpRequest::Method method;
        std::string pattern;
        std::regex expression;
        std::vector<std::string> parameterNames;
        HandlerCallback handler;
        AsyncHandlerCallback asyncHandler;
        bool isAsync;
    };

    bool addRoute(HttpRequest::Method method, const std::string& pattern,
                  const HandlerCallback& handler,
                  const AsyncHandlerCallback& asyncHandler, bool isAsync);
    static bool isDynamicPattern(const std::string& pattern);
    static void compileDynamicPattern(const std::string& pattern,
                                      std::regex* expression,
                                      std::vector<std::string>* parameterNames);
    static bool pathMatches(const Route& route, const std::string& path,
                            std::smatch* match);
    static void addAllowedMethod(std::vector<HttpRequest::Method>* methods,
                                 HttpRequest::Method method);
    static void injectParameters(HttpRequest* request, const Route& route,
                                 const std::smatch& match);
    MatchResult invoke(const Route& route, HttpRequest& request,
                       HttpResponse* response,
                       const AsyncResponder* responder) const;

    std::map<std::string, std::vector<Route> > exactRoutes_;
    std::vector<Route> dynamicRoutes_;
    std::set<std::pair<int, std::string> > registeredPatterns_;
};

} // namespace router
} // namespace http
