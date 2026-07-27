// ARServer 应用装配根对象：注册路由与中间件，并持有业务服务依赖。
#pragma once

#include <http/HttpServer.h>

class EventLoop;
class InetAddress;
class StaticFileHandler;

namespace ar {
class AuthHandler;
class SessionHandlers;
class PresenceHandlers;
class SceneInteractionHandlers;

class ARServer
{
public:
    ARServer(EventLoop* loop, const InetAddress& address, AuthHandler* auth, SessionHandlers* sessions,
             PresenceHandlers* presence, SceneInteractionHandlers* interactions, StaticFileHandler* files);
    void setThreadNum(int threads);
    void start();
private:
    HttpServer server_;
};

} // namespace ar
