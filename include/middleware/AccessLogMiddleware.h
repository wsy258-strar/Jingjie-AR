// 访问日志中间件：记录请求结果，并在输出前脱敏可能包含凭据的请求目标。
#pragma once

#include <middleware/Middleware.h>

#include <string>

class AccessLogMiddleware : public Middleware
{
public:
    /// sanitizeTarget 用于日志展示，不改变实际路由或请求对象中的目标。
    bool before(HttpRequest& request, HttpResponse& response) override;
    void after(const HttpRequest& request, HttpResponse& response) override;
    static std::string sanitizeTarget(const std::string& target);
};
