// HTTP 认证中间件实现：验证成功后仅向当前请求注入身份上下文。
#include <middleware/AuthMiddleware.h>

#include <session/SessionManager.h>

namespace ar {

bool AuthMiddleware::isPublicRequest(const HttpRequest& request)
{
    const std::string& path = request.path();
    if (path.compare(0, 5, "/api/") != 0 || path == "/api/auth" || path == "/api/scenes")
        return true;
    if (path.compare(0, 12, "/api/scenes/") != 0)
        return false;
    if (request.method() != HttpRequest::kGet) return false;
    return path.find("/members") != std::string::npos ||
           path.find("/comments") != std::string::npos ||
           path.find("/likes") == std::string::npos;
}

bool AuthMiddleware::before(HttpRequest& request, HttpResponse& response)
{
    if (isPublicRequest(request)) return true;
    const std::string token = http::session::SessionManager::extractToken(request);
    if (!token.empty())
    {
        request.setAttribute("auth.token", token);
        return true;
    }
    response.setStatusCode(HttpResponse::k401Unauthorized);
    response.setContentType("application/json; charset=utf-8");
    response.setBody("{\"error\":\"authentication token is required\",\"code\":\"UNAUTHORIZED\"}");
    return false;
}

} // namespace ar
