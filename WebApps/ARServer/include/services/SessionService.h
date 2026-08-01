// 协作会话服务：管理进入、退出和心跳状态，并维护客户端可见的在线成员语义。
#pragma once

#include <db/SessionDAO.h>

#include <functional>
#include <memory>

namespace ar {

class SessionStore
{
public:
    typedef std::function<void(const std::shared_ptr<Session>&)> SessionCallback;
    typedef std::function<void(bool)> BoolCallback;
    virtual ~SessionStore() {}
    virtual void find(const std::string& token, const SessionCallback& callback) = 0;
    virtual void enter(uint64_t sessionId, const std::string& sceneId, const BoolCallback& callback) = 0;
    virtual void exit(uint64_t sessionId, const BoolCallback& callback) = 0;
    virtual void revoke(const std::string&, const BoolCallback& callback)
    { callback(false); }
    virtual void invalidate(const std::string&) {}
};

class SessionService
{
public:
    enum EnterResult { kEnterOk, kEnterSessionNotFound, kEnterUnavailable };
    enum LogoutResult { kLogoutOk, kLogoutUnavailable };
    typedef std::function<void(EnterResult)> EnterCallback;
    typedef std::function<void(LogoutResult)> LogoutCallback;
    explicit SessionService(SessionStore* store) : store_(store) {}
    void get(const std::string& token, const SessionStore::SessionCallback& completion);
    void enter(const std::string& token, const std::string& sceneId,
               const SessionStore::BoolCallback& completion);
    void enterDetailed(const std::string& token, const std::string& sceneId,
                       const EnterCallback& completion);
    void exit(const std::string& token, const SessionStore::BoolCallback& completion);
    void logout(const std::string& token, const LogoutCallback& completion);
private:
    SessionStore* store_;
};

} // namespace ar
