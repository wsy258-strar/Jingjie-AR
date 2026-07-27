// 面向对象路由处理器接口，适用于需要封装业务状态的同步 HTTP 端点。
#pragma once

#include <http/HttpRequest.h>
#include <http/HttpResponse.h>

namespace http {
namespace router {

class RouterHandler
{
public:
    /// 处理器不得保存 response 指针以供异步使用；异步端点应使用 AsyncResponder。
    virtual ~RouterHandler() {}

    virtual void handle(const HttpRequest& request, HttpResponse* response) = 0;
};

} // namespace router
} // namespace http
