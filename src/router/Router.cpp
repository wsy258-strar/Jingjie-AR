#include <router/Router.h>

#include <algorithm>

namespace {

bool isRegexMetaCharacter(char character)
{
    switch (character)
    {
    case '\\':
    case '^':
    case '$':
    case '.':
    case '|':
    case '?':
    case '*':
    case '+':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
        return true;
    default:
        return false;
    }
}

} // namespace

namespace http {
namespace router {

Router::Route::Route(HttpRequest::Method routeMethod,
                     const std::string& routePattern,
                     const HandlerCallback& routeHandler)
    : method(routeMethod),
      pattern(routePattern),
      expression(),
      parameterNames(),
      handler(routeHandler),
      asyncHandler(),
      isAsync(false)
{
    Router::compileDynamicPattern(pattern, &expression, &parameterNames);
}

Router::Route::Route(HttpRequest::Method routeMethod,
                     const std::string& routePattern,
                     const AsyncHandlerCallback& routeHandler)
    : method(routeMethod),
      pattern(routePattern),
      expression(),
      parameterNames(),
      handler(),
      asyncHandler(routeHandler),
      isAsync(true)
{
    Router::compileDynamicPattern(pattern, &expression, &parameterNames);
}

bool Router::add(HttpRequest::Method method, const std::string& pattern,
                 const HandlerCallback& handler)
{
    return addRoute(method, pattern, handler, AsyncHandlerCallback(), false);
}

bool Router::addAsync(HttpRequest::Method method, const std::string& pattern,
                      const AsyncHandlerCallback& handler)
{
    return addRoute(method, pattern, HandlerCallback(), handler, true);
}

bool Router::addRoute(HttpRequest::Method method, const std::string& pattern,
                      const HandlerCallback& handler,
                      const AsyncHandlerCallback& asyncHandler, bool isAsync)
{
    const std::pair<int, std::string> key(static_cast<int>(method), pattern);
    if (registeredPatterns_.find(key) != registeredPatterns_.end())
    {
        return false;
    }

    registeredPatterns_.insert(key);
    if (isDynamicPattern(pattern))
    {
        if (isAsync)
        {
            dynamicRoutes_.push_back(Route(method, pattern, asyncHandler));
        }
        else
        {
            dynamicRoutes_.push_back(Route(method, pattern, handler));
        }
        return true;
    }

    if (isAsync)
    {
        exactRoutes_[pattern].push_back(Route(method, pattern, asyncHandler));
    }
    else
    {
        exactRoutes_[pattern].push_back(Route(method, pattern, handler));
    }
    return true;
}

MatchResult Router::route(HttpRequest& request, HttpResponse* response,
                          std::vector<HttpRequest::Method>* allowedMethods,
                          const AsyncResponder* responder) const
{
    allowedMethods->clear();

    std::map<std::string, std::vector<Route> >::const_iterator exact =
        exactRoutes_.find(request.path());
    if (exact != exactRoutes_.end())
    {
        const std::vector<Route>& routes = exact->second;
        for (std::vector<Route>::const_iterator it = routes.begin();
             it != routes.end(); ++it)
        {
            if (it->method == request.method())
            {
                return invoke(*it, request, response, responder);
            }
            addAllowedMethod(allowedMethods, it->method);
        }
        return kMethodNotAllowed;
    }

    for (std::vector<Route>::const_iterator it = dynamicRoutes_.begin();
         it != dynamicRoutes_.end(); ++it)
    {
        std::smatch match;
        if (!pathMatches(*it, request.path(), &match))
        {
            continue;
        }

        if (it->method == request.method())
        {
            injectParameters(&request, *it, match);
            return invoke(*it, request, response, responder);
        }
        addAllowedMethod(allowedMethods, it->method);
    }

    return allowedMethods->empty() ? kNotFound : kMethodNotAllowed;
}

bool Router::isDynamicPattern(const std::string& pattern)
{
    for (std::string::size_type index = 0; index < pattern.size(); ++index)
    {
        if (pattern[index] == ':' &&
            (index == 0 || pattern[index - 1] == '/') &&
            index + 1 < pattern.size() && pattern[index + 1] != '/')
        {
            return true;
        }
    }
    return false;
}

void Router::compileDynamicPattern(const std::string& pattern,
                                   std::regex* expression,
                                   std::vector<std::string>* parameterNames)
{
    std::string expressionText("^");
    for (std::string::size_type index = 0; index < pattern.size();)
    {
        const bool beginsParameter = pattern[index] == ':' &&
            (index == 0 || pattern[index - 1] == '/') &&
            index + 1 < pattern.size() && pattern[index + 1] != '/';
        if (beginsParameter)
        {
            const std::string::size_type end = pattern.find('/', index);
            const std::string::size_type parameterEnd =
                end == std::string::npos ? pattern.size() : end;
            parameterNames->push_back(pattern.substr(index + 1,
                                                      parameterEnd - index - 1));
            expressionText += "([^/]+)";
            index = parameterEnd;
            continue;
        }

        if (isRegexMetaCharacter(pattern[index]))
        {
            expressionText += '\\';
        }
        expressionText += pattern[index];
        ++index;
    }
    expressionText += '$';
    *expression = std::regex(expressionText);
}

bool Router::pathMatches(const Route& route, const std::string& path,
                         std::smatch* match)
{
    return std::regex_match(path, *match, route.expression);
}

void Router::addAllowedMethod(std::vector<HttpRequest::Method>* methods,
                              HttpRequest::Method method)
{
    if (std::find(methods->begin(), methods->end(), method) == methods->end())
    {
        methods->push_back(method);
    }
}

void Router::injectParameters(HttpRequest* request, const Route& route,
                              const std::smatch& match)
{
    for (std::vector<std::string>::size_type index = 0;
         index < route.parameterNames.size(); ++index)
    {
        request->setPathParameter(route.parameterNames[index],
                                  match[index + 1].str());
    }
}

MatchResult Router::invoke(const Route& route, HttpRequest& request,
                           HttpResponse* response,
                           const AsyncResponder* responder) const
{
    HttpRequest routedRequest(request);
    if (route.isAsync)
    {
        if (responder != 0)
        {
            route.asyncHandler(routedRequest, *responder);
        }
        return kAsyncPending;
    }

    route.handler(routedRequest, response);
    return kHandled;
}

} // namespace router
} // namespace http
