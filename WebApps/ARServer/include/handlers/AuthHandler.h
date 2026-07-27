// 认证端点处理器：接收注册/登录凭据并委托 AuthService 异步返回会话令牌。
#pragma once

#include <http/HttpRequest.h>
#include <http/HttpResponse.h>
#include <http/AsyncResponder.h>

namespace ar { class AuthService; }

namespace ar {

class AuthHandler
{
public:
    explicit AuthHandler(AuthService* service) : service_(service) {}
    // JSON body credentials take precedence; query parameters remain a legacy fallback.
    static bool credentials(const HttpRequest& request, std::string* username, std::string* password);
    // Returns true when the request contains both required credentials.
    static bool validate(const HttpRequest& request, HttpResponse* response);
    void handle(const HttpRequest& request, const AsyncResponder& responder) const;
private:
    AuthService* service_;
};

} // namespace ar
