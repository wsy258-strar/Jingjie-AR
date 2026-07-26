#pragma once

#include <services/SessionService.h>

class TaskWorkerPool;
class SessionCache;

namespace ar {

class SessionCachePort
{
public:
    virtual ~SessionCachePort() {}
    virtual bool get(const std::string& token, Session& output) = 0;
    virtual bool put(const Session& session) = 0;
    virtual bool remove(const std::string& token) = 0;
};

class SessionCacheAdapter : public SessionCachePort
{
public:
    explicit SessionCacheAdapter(SessionCache* cache) : cache_(cache) {}
    bool get(const std::string& token, Session& output) override;
    bool put(const Session& session) override;
    bool remove(const std::string& token) override;
private:
    SessionCache* cache_;
};

class CachedSessionStore : public SessionStore
{
public:
    CachedSessionStore(SessionStore* durable, SessionCachePort* cache, TaskWorkerPool* workers)
        : durable_(durable), cache_(cache), workers_(workers) {}
    void find(const std::string& token, const SessionCallback& callback) override;
    void enter(uint64_t sessionId, const std::string& sceneId, const BoolCallback& callback) override;
    void exit(uint64_t sessionId, const BoolCallback& callback) override;
    void invalidate(const std::string& token) override;
private:
    SessionStore* durable_;
    SessionCachePort* cache_;
    TaskWorkerPool* workers_;
};

} // namespace ar
