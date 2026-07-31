// ARServer 应用装配根对象：注册路由与中间件，并持有业务服务依赖。
#pragma once

#include <http/HttpServer.h>

#include <string>

class EventLoop;
class InetAddress;
class StaticFileHandler;

namespace ar {
class AuthHandler;
class SceneHandlers;
class ArtworkInteractionHandlers;
class VisitorHandlers;

class ARServer
{
public:
    ARServer(EventLoop* loop, const InetAddress& address,
             const std::string& allowedOrigin,
             AuthHandler* auth,
             VisitorHandlers* visitors,
             SceneHandlers* scenes,
             ArtworkInteractionHandlers* artworks,
             StaticFileHandler* files);
    void setThreadNum(int threads);
    void start();
private:
    HttpServer server_;
};

} // namespace ar
