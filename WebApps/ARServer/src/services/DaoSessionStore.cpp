// 会话持久化 DAO 适配实现。
#include <services/DaoSessionStore.h>

namespace ar {

void DaoSessionStore::find(const std::string& token, const SessionCallback& callback)
{
    if (dao_) dao_->findSessionByToken(token, callback);
    else callback(std::shared_ptr<Session>());
}

void DaoSessionStore::enter(uint64_t sessionId, const std::string& sceneId, const BoolCallback& callback)
{
    if (dao_) dao_->updateSessionScene(sessionId, sceneId, callback);
    else callback(false);
}

void DaoSessionStore::exit(uint64_t sessionId, const BoolCallback& callback)
{
    if (dao_) dao_->endSession(sessionId, callback);
    else callback(false);
}

void DaoSessionStore::revoke(const std::string& token, const BoolCallback& callback)
{
    if (dao_) dao_->revokeSession(token, callback);
    else callback(false);
}

} // namespace ar
