// 认证端点处理器：接收注册/登录凭据并委托 AuthService 异步返回会话令牌。
#pragma once

#include <http/HttpRequest.h>
#include <http/HttpResponse.h>
#include <http/AsyncResponder.h>

namespace ar { class AuthService; class SessionService; }

namespace ar {

class AuthHandler
{
public:
    explicit AuthHandler(AuthService* service, SessionService* sessions = 0)
        : service_(service), sessions_(sessions) {}
    // JSON body credentials take precedence; query parameters remain a legacy fallback.
    static bool credentials(const HttpRequest& request, std::string* username, std::string* password);
    // Returns true when the request contains both required credentials.
    static bool validate(const HttpRequest& request, HttpResponse* response);
    void handle(const HttpRequest& request, const AsyncResponder& responder) const;
    // Bearer 登出是幂等撤销：已失效会话仍返回成功，存储故障则返回 503。
    void logout(const HttpRequest& request, const AsyncResponder& responder) const;
private:
    AuthService* service_;
    SessionService* sessions_;
};

} // namespace ar
