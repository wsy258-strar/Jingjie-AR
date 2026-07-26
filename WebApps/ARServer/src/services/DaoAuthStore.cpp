#include <services/DaoAuthStore.h>

namespace ar {

void DaoAuthStore::findUser(const std::string& username, const UserCallback& callback)
{
    if (dao_) dao_->findUserByUsername(username, callback);
    else callback(std::shared_ptr<User>());
}

void DaoAuthStore::createUser(const std::string& username, const std::string& password,
                              const IdCallback& callback)
{
    if (dao_) dao_->createUser(username, password, callback);
    else callback(0);
}

void DaoAuthStore::createSession(uint64_t userId, const std::string& token,
                                 const IdCallback& callback)
{
    if (dao_) dao_->createSession(userId, token, "", callback);
    else callback(0);
}

} // namespace ar
