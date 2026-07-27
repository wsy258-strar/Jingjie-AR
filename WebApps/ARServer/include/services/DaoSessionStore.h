// 会话 DAO 适配器：把 SessionDAO 的回调接口映射为应用层会话持久化操作。
#pragma once

#include <services/SessionService.h>

namespace ar {

class DaoSessionStore : public SessionStore
{
public:
    explicit DaoSessionStore(SessionDAO* dao) : dao_(dao) {}
    void find(const std::string& token, const SessionCallback& callback) override;
    void enter(uint64_t sessionId, const std::string& sceneId, const BoolCallback& callback) override;
    void exit(uint64_t sessionId, const BoolCallback& callback) override;
private:
    SessionDAO* dao_;
};

} // namespace ar
