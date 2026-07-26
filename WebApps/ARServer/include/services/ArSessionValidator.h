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
