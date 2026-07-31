// HTTP 认证中间件实现：验证成功后仅向当前请求注入身份上下文。
#include <middleware/AuthMiddleware.h>

#include <utils/ApiError.h>

namespace {

std::string bearerToken(const HttpRequest& request)
{
    const std::string authorization = request.getHeader("Authorization");
    const std::string prefix = "Bearer ";
    if (authorization.compare(0, prefix.size(), prefix) != 0 ||
        authorization.size() == prefix.size())
        return std::string();
    return authorization.substr(prefix.size());
}

bool startsWith(const std::string& value, const std::string& prefix)
{
    return value.compare(0, prefix.size(), prefix) == 0;
}

bool artworkRoute(const std::string& path, const std::string& suffix)
{
    const std::string prefix = "/api/artworks/";
    if (!startsWith(path, prefix)) return false;
    const std::string::size_type idBegin = prefix.size();
    const std::string::size_type slash = path.find('/', idBegin);
    if (slash == idBegin) return false;
    if (suffix.empty()) return slash == std::string::npos;
    return slash != std::string::npos && path.substr(slash) == suffix;
}

} // namespace

namespace ar {

bool AuthMiddleware::isPublicRequest(const HttpRequest& request)
{
    const std::string& path = request.path();
    if (!startsWith(path, "/api/")) return true;
    if (path == "/api/auth" || path == "/api/visitors/session") return true;
    if (request.method() != HttpRequest::kGet) return false;
    if (path == "/api/scenes" || path == "/api/presence" ||
        path == "/api/statistics/views")
        return true;
    if (startsWith(path, "/api/scenes/") && path.find('/', 12) == std::string::npos)
        return true;
    return artworkRoute(path, std::string()) || artworkRoute(path, "/comments");
}

bool AuthMiddleware::before(HttpRequest& request, HttpResponse& response)
{
    // 公开详情接口也需要可选用户身份，因此必须先解析 Bearer，再判断权限。
    const std::string token = bearerToken(request);
    if (!token.empty()) request.setAttribute("auth.token", token);
    if (isPublicRequest(request)) return true;

    const std::string& path = request.path();
    if ((path == "/api/presence/heartbeat" || path == "/api/presence/exit") &&
        !request.getHeader("X-Visitor-Token").empty())
    {
        return true;
    }

    if (!token.empty() &&
        ((request.method() == HttpRequest::kPost &&
          (artworkRoute(path, "/likes") || artworkRoute(path, "/comments"))) ||
         (request.method() == HttpRequest::kDelete && artworkRoute(path, "/likes"))))
        return true;

    response = makeApiError(HttpResponse::k401Unauthorized, "UNAUTHORIZED",
                            "required authentication token is missing",
                            request.attribute("request_id"));
    return false;
}

} // namespace ar
