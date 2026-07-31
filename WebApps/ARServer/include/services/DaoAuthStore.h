// 用户认证 DAO 适配器：将底层异步 User 查询封装为 AuthService 所需的存储契约。
#pragma once

#include <services/AuthService.h>

namespace ar {

class DaoAuthStore : public AuthStore
{
public:
    explicit DaoAuthStore(SessionDAO* dao) : dao_(dao) {}
    void findUser(const std::string& username, const UserCallback& callback) override;
    void createUser(const std::string& username, const std::string& hash,
                    const IdCallback& callback) override;
    void updatePasswordHash(uint64_t userId, const std::string& hash,
                            const std::function<void(bool)>& callback) override;
    void createSession(uint64_t userId, const std::string& token,
                       const IdCallback& callback) override;
private:
    SessionDAO* dao_;
};

} // namespace ar
