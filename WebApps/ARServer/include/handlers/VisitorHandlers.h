// 匿名访客 HTTP 处理器：负责初始化幂等、展馆在线状态和总浏览量响应。
#pragma once

#include <http/AsyncResponder.h>
#include <http/HttpRequest.h>

#include <string>

class ExhibitionStatisticsDAO;
class TaskWorkerPool;

namespace ar {

class PresenceService;
class VisitorSessionService;

class VisitorHandlers
{
public:
    VisitorHandlers(VisitorSessionService* visitors,
                    PresenceService* presence,
                    ExhibitionStatisticsDAO* statistics,
                    TaskWorkerPool* cacheWorkers,
                    const std::string& exhibitionId)
        : visitors_(visitors),
          presence_(presence),
          statistics_(statistics),
          cacheWorkers_(cacheWorkers),
          exhibitionId_(exhibitionId)
    {
    }

    void bootstrap(const HttpRequest& request, const AsyncResponder& responder) const;
    void heartbeat(const HttpRequest& request, const AsyncResponder& responder) const;
    void exit(const HttpRequest& request, const AsyncResponder& responder) const;
    void presence(const HttpRequest& request, const AsyncResponder& responder) const;
    void views(const HttpRequest& request, const AsyncResponder& responder) const;

private:
    VisitorSessionService* visitors_;
    PresenceService* presence_;
    ExhibitionStatisticsDAO* statistics_;
    TaskWorkerPool* cacheWorkers_;
    std::string exhibitionId_;
};

} // namespace ar
