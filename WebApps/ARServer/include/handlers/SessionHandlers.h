#pragma once

#include <db/SessionDAO.h>
#include <http/AsyncResponder.h>
#include <http/HttpRequest.h>

#include <string>

class TaskWorkerPool;

namespace ar {
class SessionService;
class PresenceService;

class SessionHandlers
{
public:
    explicit SessionHandlers(SessionService* service, PresenceService* presence = 0,
                             TaskWorkerPool* cacheWorkers = 0, int testDbDelayMs = 0)
        : service_(service), presence_(presence), cacheWorkers_(cacheWorkers),
          testDbDelayMs_(testDbDelayMs) {}
    static std::string json(const Session& session);
    void get(const HttpRequest& request, const AsyncResponder& responder) const;
    void enter(const HttpRequest& request, const AsyncResponder& responder) const;
    void exit(const HttpRequest& request, const AsyncResponder& responder) const;
private:
    SessionService* service_;
    PresenceService* presence_;
    TaskWorkerPool* cacheWorkers_;
    int testDbDelayMs_;
};

} // namespace ar
