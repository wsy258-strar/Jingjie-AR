// CORS 中间件：校验跨域请求并为普通请求或预检请求写入响应头。
#pragma once

#include <middleware/Middleware.h>
#include <middleware/cors/CorsConfig.h>

class CorsMiddleware : public Middleware
{
public:
    /// 配置在构造时复制，之后只读，便于多个 I/O 线程共享同一中间件实例。
    explicit CorsMiddleware(const CorsConfig& config);

    bool before(HttpRequest& request, HttpResponse& response) override;
    void after(const HttpRequest& request, HttpResponse& response) override;

private:
    bool isOriginAllowed(const std::string& origin) const;
    bool isMethodAllowed(const std::string& method) const;
    bool areHeadersAllowed(const std::string& headers) const;
    void addCorsHeaders(const std::string& origin, HttpResponse& response,
                        bool preflight) const;

    CorsConfig config_;
};
