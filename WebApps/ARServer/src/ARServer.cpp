// ARServer 路由和依赖装配实现：将框架 HTTP 抽象连接到业务处理器。
#include <ARServer.h>

#include <handlers/AuthHandler.h>
#include <handlers/SessionHandlers.h>
#include <handlers/SceneHandlers.h>
#include <handlers/PresenceHandlers.h>
#include <handlers/SceneInteractionHandlers.h>
#include <middleware/AuthMiddleware.h>
#include <http/StaticFileHandler.h>
#include <middleware/AccessLogMiddleware.h>
#include <middleware/RequestIdMiddleware.h>
#include <middleware/cors/CorsMiddleware.h>

#include <memory>

namespace ar {

ARServer::ARServer(EventLoop* loop, const InetAddress& address, AuthHandler* auth, SessionHandlers* sessions,
                   PresenceHandlers* presence, SceneHandlers* scenes,
                   SceneInteractionHandlers* interactions, StaticFileHandler* files)
    : server_(loop, address, "ARServer")
{
    CorsConfig cors;
    cors.allowedOrigins.push_back("*");
    cors.allowedMethods.push_back("GET");
    cors.allowedMethods.push_back("POST");
    cors.allowedMethods.push_back("DELETE");
    cors.allowedMethods.push_back("OPTIONS");
    cors.allowedHeaders.push_back("Content-Type");
    cors.allowedHeaders.push_back("Authorization");
    cors.maxAge = 600;
    server_.addMiddleware(std::shared_ptr<Middleware>(new CorsMiddleware(cors)));
    server_.addMiddleware(std::shared_ptr<Middleware>(new RequestIdMiddleware()));
    server_.addMiddleware(std::shared_ptr<Middleware>(new AccessLogMiddleware()));
    server_.addMiddleware(std::shared_ptr<Middleware>(new AuthMiddleware()));
    if (auth)
    {
        server_.PostAsync("/api/auth", [auth](const HttpRequest& request,
                                                const AsyncResponder& responder) {
            auth->handle(request, responder);
        });
    }
    if (presence)
    {
        server_.PostAsync("/api/session/heartbeat", [presence](const HttpRequest& request,
                                                                 const AsyncResponder& responder) {
            presence->heartbeat(request, responder);
        });
        server_.GetAsync("/api/scenes/:sceneId/members", [presence](const HttpRequest& request,
                                                                     const AsyncResponder& responder) {
            presence->members(request, responder);
        });
    }
    if (sessions)
    {
        server_.GetAsync("/api/session", [sessions](const HttpRequest& request,
                                                      const AsyncResponder& responder) {
            sessions->get(request, responder);
        });
        server_.PostAsync("/api/session/enter", [sessions](const HttpRequest& request,
                                                             const AsyncResponder& responder) {
            sessions->enter(request, responder);
        });
        server_.PostAsync("/api/session/exit", [sessions](const HttpRequest& request,
                                                            const AsyncResponder& responder) {
            sessions->exit(request, responder);
        });
    }
    server_.Get("/api/scenes", [scenes](const HttpRequest& request, HttpResponse* response) {
        scenes->list(request, response);
    });
    server_.Get("/api/scenes/:sceneId",
                [scenes](const HttpRequest& request, HttpResponse* response) {
        scenes->get(request, response);
    });
    if (interactions)
    {
        server_.GetAsync("/api/scenes/:sceneId/comments", [interactions](const HttpRequest& request,
                                                                           const AsyncResponder& responder) {
            interactions->comments(request, responder);
        });
        server_.PostAsync("/api/scenes/:sceneId/likes", [interactions](const HttpRequest& request,
                                                                         const AsyncResponder& responder) {
            interactions->like(request, responder);
        });
        server_.DeleteAsync("/api/scenes/:sceneId/likes", [interactions](const HttpRequest& request,
                                                                           const AsyncResponder& responder) {
            interactions->unlike(request, responder);
        });
        server_.PostAsync("/api/scenes/:sceneId/comments", [interactions](const HttpRequest& request,
                                                                            const AsyncResponder& responder) {
            interactions->comment(request, responder);
        });
    }
    server_.Post("/api/scenes/:sceneId/interactions", SceneHandlers::interactions);
    server_.setAsyncFallback([files](const HttpRequest& request, const AsyncResponder& responder) {
        if (files && (request.method() == HttpRequest::kGet || request.method() == HttpRequest::kHead))
        {
            files->handleAsync(request, responder);
            return;
        }
        HttpResponse response(false);
        response.setStatusCode(HttpResponse::k404NotFound);
        responder.send(response);
    });
}

void ARServer::setThreadNum(int threads) { server_.setThreadNum(threads); }
void ARServer::start() { server_.start(); }

} // namespace ar
