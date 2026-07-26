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
    virtual void invalidate(const std::string&) {}
};

class SessionService
{
public:
    explicit SessionService(SessionStore* store) : store_(store) {}
    void get(const std::string& token, const SessionStore::SessionCallback& completion);
    void enter(const std::string& token, const std::string& sceneId,
               const SessionStore::BoolCallback& completion);
    void exit(const std::string& token, const SessionStore::BoolCallback& completion);
private:
    SessionStore* store_;
};

} // namespace ar
