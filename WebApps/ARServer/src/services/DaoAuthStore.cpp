// 认证存储 DAO 适配实现。
#include <services/DaoAuthStore.h>

namespace ar {

void DaoAuthStore::findUser(const std::string& username, const UserCallback& callback)
{
    if (dao_) dao_->findUserByUsername(username, callback);
    else callback(std::shared_ptr<User>());
}

void DaoAuthStore::createUser(const std::string& username, const std::string& hash,
                              const IdCallback& callback)
{
    if (dao_) dao_->createUser(username, hash, callback);
    else callback(0);
}

void DaoAuthStore::updatePasswordHash(uint64_t userId, const std::string& hash,
                                      const std::function<void(bool)>& callback)
{
    if (dao_) dao_->updatePasswordHash(userId, hash, callback);
    else callback(false);
}

void DaoAuthStore::createSession(uint64_t userId, const std::string& token,
                                 const IdCallback& callback)
{
    if (dao_) dao_->createSession(userId, token, "", callback);
    else callback(0);
}

} // namespace ar
