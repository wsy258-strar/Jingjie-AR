#pragma once

#include <services/AuthService.h>

namespace ar {

class DaoAuthStore : public AuthStore
{
public:
    explicit DaoAuthStore(SessionDAO* dao) : dao_(dao) {}
    void findUser(const std::string& username, const UserCallback& callback) override;
    void createUser(const std::string& username, const std::string& password,
                    const IdCallback& callback) override;
    void createSession(uint64_t userId, const std::string& token,
                       const IdCallback& callback) override;
private:
    SessionDAO* dao_;
};

} // namespace ar
