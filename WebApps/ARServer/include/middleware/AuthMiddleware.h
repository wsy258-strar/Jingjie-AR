// 认证中间件：从请求中提取会话令牌，并将已验证身份写入本次请求属性。
#pragma once

#include <middleware/Middleware.h>

namespace ar {

class AuthMiddleware : public Middleware
{
public:
    bool before(HttpRequest& request, HttpResponse& response) override;
    void after(const HttpRequest&, HttpResponse&) override {}
private:
    static bool isPublicRequest(const HttpRequest& request);
};

} // namespace ar
