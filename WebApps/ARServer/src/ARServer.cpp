// ARServer 路由和依赖装配实现：将框架 HTTP 抽象连接到业务处理器。
#include <ARServer.h>

#include <handlers/ArtworkInteractionHandlers.h>
#include <handlers/AuthHandler.h>
#include <handlers/SceneHandlers.h>
#include <handlers/VisitorHandlers.h>
#include <middleware/AuthMiddleware.h>
#include <http/StaticFileHandler.h>
#include <middleware/AccessLogMiddleware.h>
#include <middleware/RequestIdMiddleware.h>
#include <middleware/cors/CorsMiddleware.h>

#include <memory>

namespace ar {

ARServer::ARServer(EventLoop* loop, const InetAddress& address,
                   const std::string& allowedOrigin,
                   AuthHandler* auth,
                   VisitorHandlers* visitors,
                   SceneHandlers* scenes,
                   ArtworkInteractionHandlers* artworks,
                   StaticFileHandler* files)
    : server_(loop, address, "ARServer")
{
    if (!allowedOrigin.empty())
    {
        CorsConfig cors;
        cors.allowedOrigins.push_back(allowedOrigin);
        cors.allowedMethods.push_back("GET");
        cors.allowedMethods.push_back("POST");
        cors.allowedMethods.push_back("DELETE");
        cors.allowedMethods.push_back("OPTIONS");
        cors.allowedHeaders.push_back("Content-Type");
        cors.allowedHeaders.push_back("Authorization");
        cors.allowedHeaders.push_back("X-Visitor-Token");
        cors.maxAge = 600;
        server_.addMiddleware(std::shared_ptr<Middleware>(new CorsMiddleware(cors)));
    }
    server_.addMiddleware(std::shared_ptr<Middleware>(new RequestIdMiddleware()));
    server_.addMiddleware(std::shared_ptr<Middleware>(new AccessLogMiddleware()));
    server_.addMiddleware(std::shared_ptr<Middleware>(new AuthMiddleware()));
    if (auth)
    {
        server_.PostAsync("/api/auth", [auth](const HttpRequest& request,
                                                const AsyncResponder& responder) {
            auth->handle(request, responder);
        });
        server_.PostAsync("/api/auth/logout", [auth](const HttpRequest& request,
                                                       const AsyncResponder& responder) {
            auth->logout(request, responder);
        });
    }
    if (visitors)
    {
        server_.PostAsync("/api/visitors/session", [visitors](const HttpRequest& request,
                                                                const AsyncResponder& responder) {
            visitors->bootstrap(request, responder);
        });
        server_.PostAsync("/api/presence/heartbeat", [visitors](const HttpRequest& request,
                                                                  const AsyncResponder& responder) {
            visitors->heartbeat(request, responder);
        });
        server_.PostAsync("/api/presence/exit", [visitors](const HttpRequest& request,
                                                             const AsyncResponder& responder) {
            visitors->exit(request, responder);
        });
        server_.GetAsync("/api/presence", [visitors](const HttpRequest& request,
                                                       const AsyncResponder& responder) {
            visitors->presence(request, responder);
        });
        server_.GetAsync("/api/statistics/views", [visitors](const HttpRequest& request,
                                                               const AsyncResponder& responder) {
            visitors->views(request, responder);
        });
    }
    if (scenes)
    {
        server_.Get("/api/scenes", [scenes](const HttpRequest& request, HttpResponse* response) {
            scenes->list(request, response);
        });
        server_.Get("/api/scenes/:sceneId",
                    [scenes](const HttpRequest& request, HttpResponse* response) {
            scenes->get(request, response);
        });
    }
    if (artworks)
    {
        server_.GetAsync("/api/artworks/:artworkId", [artworks](const HttpRequest& request,
                                                                  const AsyncResponder& responder) {
            artworks->detail(request, responder);
        });
        server_.PostAsync("/api/artworks/:artworkId/likes", [artworks](const HttpRequest& request,
                                                                         const AsyncResponder& responder) {
            artworks->like(request, responder);
        });
        server_.DeleteAsync("/api/artworks/:artworkId/likes", [artworks](const HttpRequest& request,
                                                                           const AsyncResponder& responder) {
            artworks->unlike(request, responder);
        });
        server_.GetAsync("/api/artworks/:artworkId/comments", [artworks](const HttpRequest& request,
                                                                            const AsyncResponder& responder) {
            artworks->comments(request, responder);
        });
        server_.PostAsync("/api/artworks/:artworkId/comments", [artworks](const HttpRequest& request,
                                                                             const AsyncResponder& responder) {
            artworks->comment(request, responder);
        });
    }
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
