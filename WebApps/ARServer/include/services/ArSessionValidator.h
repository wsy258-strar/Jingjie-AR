// AR 会话验证器：集中定义 Token 校验、用户身份提取与无效会话的失败语义。
#pragma once

#include <session/SessionManager.h>

namespace ar {

class SessionService;

class ArSessionValidator : public http::session::SessionValidator
{
public:
    explicit ArSessionValidator(SessionService* sessions) : sessions_(sessions) {}
    void validate(const std::string& token,
                  const std::function<void(bool)>& completion) override;
private:
    SessionService* sessions_;
};

} // namespace ar
