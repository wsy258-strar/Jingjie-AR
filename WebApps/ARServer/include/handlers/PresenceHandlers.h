// 在线成员端点处理器：将场景成员读取请求交给 PresenceService 并输出 JSON。
#pragma once

#include <http/AsyncResponder.h>
#include <http/HttpRequest.h>

class TaskWorkerPool;

namespace ar {
class PresenceService;
class SessionService;

class PresenceHandlers
{
public:
    PresenceHandlers(PresenceService* presence, SessionService* sessions, TaskWorkerPool* cacheWorkers)
        : presence_(presence), sessions_(sessions), cacheWorkers_(cacheWorkers) {}
    void heartbeat(const HttpRequest& request, const AsyncResponder& responder) const;
    void members(const HttpRequest& request, const AsyncResponder& responder) const;
private:
    PresenceService* presence_;
    SessionService* sessions_;
    TaskWorkerPool* cacheWorkers_;
};

} // namespace ar
