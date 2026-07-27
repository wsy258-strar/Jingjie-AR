// HTTP 中间件契约：before 可短路请求，after 仅对已成功进入 before 的中间件逆序执行。
#pragma once

#include <http/HttpRequest.h>
#include <http/HttpResponse.h>

class Middleware
{
public:
    /// before 返回 false 时，链路停止，response 应已由该中间件填充。
    virtual ~Middleware() {}

    virtual bool before(HttpRequest& request, HttpResponse& response) = 0;
    virtual void after(const HttpRequest& request, HttpResponse& response) = 0;
};
