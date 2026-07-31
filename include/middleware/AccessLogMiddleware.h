// 访问日志中间件：只记录不含查询参数的路径，避免凭据进入日志。
#pragma once

#include <middleware/Middleware.h>

#include <string>

class AccessLogMiddleware : public Middleware
{
public:
    /// 返回不含 query string 的日志路径，不改变实际路由或请求对象。
    bool before(HttpRequest& request, HttpResponse& response) override;
    void after(const HttpRequest& request, HttpResponse& response) override;
    static std::string sanitizeTarget(const std::string& target);
};
