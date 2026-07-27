// HTTP 服务器外观：把 TcpServer 的字节流事件转换为请求分发与响应发送流程。
#pragma once

#include <functional>
#include <string>
#include <memory>

#include <net/TcpServer.h>
#include <net/InetAddress.h>
#include <net/Buffer.h>
#include <net/Callbacks.h>
#include <http/HttpRequest.h>
#include <http/HttpResponse.h>
#include <http/HttpContext.h>
#include <http/HttpDispatcher.h>
#include <http/HttpLimits.h>
#include <base/noncopyable.h>

/**
 * @brief HTTP服务器外观类
 *
 * 封装TcpServer，提供HTTP协议层处理。
 * 使用方式(类似EchoServer):
 *   HttpServer server(&loop, addr, "HttpServer");
 *   server.setHttpCallback([](const HttpRequest& req, HttpResponse* resp) { ... });
 *   server.setThreadNum(3);
 *   server.start();
 */
class HttpServer : noncopyable
{
public:
    using HttpCallback = std::function<void(const HttpRequest&, HttpResponse*)>;

    HttpServer(EventLoop* loop,
               const InetAddress& listenAddr,
               const std::string& name);

    bool Get(const std::string& pattern, const HttpCallback& callback)
    { return dispatcher_.Get(pattern, callback); }
    bool Post(const std::string& pattern, const HttpCallback& callback)
    { return dispatcher_.Post(pattern, callback); }
    bool Put(const std::string& pattern, const HttpCallback& callback)
    { return dispatcher_.Put(pattern, callback); }
    bool Delete(const std::string& pattern, const HttpCallback& callback)
    { return dispatcher_.Delete(pattern, callback); }
    bool Options(const std::string& pattern, const HttpCallback& callback)
    { return dispatcher_.Options(pattern, callback); }
    bool GetAsync(const std::string& pattern, const HttpDispatcher::AsyncCallback& callback)
    { return dispatcher_.GetAsync(pattern, callback); }
    bool PostAsync(const std::string& pattern, const HttpDispatcher::AsyncCallback& callback)
    { return dispatcher_.PostAsync(pattern, callback); }
    bool DeleteAsync(const std::string& pattern, const HttpDispatcher::AsyncCallback& callback)
    { return dispatcher_.DeleteAsync(pattern, callback); }

    void addMiddleware(const std::shared_ptr<Middleware>& middleware)
    { dispatcher_.addMiddleware(middleware); }
    void setFallback(const HttpCallback& callback) { dispatcher_.setFallback(callback); }
    void setAsyncFallback(const HttpDispatcher::AsyncCallback& callback)
    { dispatcher_.setAsyncFallback(callback); }
    void setHttpCallback(const HttpCallback& callback) { setFallback(callback); }
    void setLimits(const HttpLimits& limits) { limits_ = limits; }

    /// 设置工作线程数(subLoop数量)
    void setThreadNum(int numThreads) { server_.setThreadNum(numThreads); }

    /// 可选的连接事件回调(connect/disconnect)，在HTTP处理之外额外通知
    void setConnectionCallback(const ConnectionCallback& cb)
    { connectionCallback_ = cb; }

    /// 启动HTTP服务器(开始监听)
    void start() { server_.start(); }

    /// 访问底层TcpServer(高级用法)
    TcpServer& tcpServer() { return server_; }

    /// 判断 Keep-Alive 连接能否继续解析下一请求；异步响应或关闭语义会暂停当前循环。
    static bool shouldContinueParsing(HttpDispatcher::Result result,
                                      bool closeConnection);

private:
    void onConnection(const TcpConnectionPtr& conn);
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp receiveTime);
    HttpDispatcher::Result onRequest(const TcpConnectionPtr& conn,
                                     const HttpRequest& req,
                                     bool* closeConnection);
    HttpDispatcher::AsyncResponderFactory asyncResponderFactory(
        const TcpConnectionPtr& conn, bool closeConnection);
    static void sendResponse(const TcpConnectionPtr& conn, HttpResponse response);
    void sendParseError(const TcpConnectionPtr& conn, HttpContext::ParseError error);

    TcpServer server_;
    HttpDispatcher dispatcher_;
    HttpLimits limits_;
    ConnectionCallback connectionCallback_;
};
