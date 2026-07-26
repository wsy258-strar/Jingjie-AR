#include <http/HttpServer.h>
#include <http/HttpContext.h>
#include <Logger.h>

#include <fcntl.h>
#include <unistd.h>

HttpServer::HttpServer(EventLoop* loop,
                       const InetAddress& listenAddr,
                       const std::string& name)
    : server_(loop, listenAddr, name)
{
    server_.setConnectionCallback(
        std::bind(&HttpServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback(
        std::bind(&HttpServer::onMessage, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

// ========== 连接回调 ==========

void HttpServer::onConnection(const TcpConnectionPtr& conn)
{
    if (conn->connected())
    {
        LOG_INFO << "HTTP connection UP   : " << conn->peerAddress().toIpPort();
        conn->getContext().setLimits(limits_);
    }
    else
    {
        LOG_INFO << "HTTP connection DOWN : " << conn->peerAddress().toIpPort();
        // HttpContext 随 TcpConnection 生命周期自动销毁，无需手动清理
    }

    // 传递给用户自定义的连接回调
    if (connectionCallback_)
    {
        connectionCallback_(conn);
    }
}

// ========== 消息回调 (核心分发逻辑) ==========

void HttpServer::onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp receiveTime)
{
    // 获取该连接的HttpContext (每连接独立，无线程竞争)
    HttpContext& context = conn->getContext();//getContext()返回的是HTTP 协议层上下文(每连接独立的解析状态，避免多线程竞争)

    while (buf->readableBytes() > 0)
    {
        if (!context.parseRequest(buf, receiveTime))
        {
            sendParseError(conn, context.error());
            return;
        }
        if (!context.gotAll())
        {
            return;
        }
        bool closeConnection = false;
        const HttpDispatcher::Result result =
            onRequest(conn, context.request(), &closeConnection);
        if (!shouldContinueParsing(result, closeConnection))
        {
            // An asynchronous handler has already copied the request it needs.
            // Reset the per-connection parser before returning so that a later
            // Keep-Alive request is not dispatched as this completed request.
            if (result == HttpDispatcher::kAsyncPending)
            {
                context.reset();
            }
            return;
        }
        context.reset();
    }
}

// ========== 请求分发 ==========
//主要做两件事：决定是否保持连接，然后回调用户。

HttpDispatcher::Result HttpServer::onRequest(const TcpConnectionPtr& conn,
                                              const HttpRequest& req,
                                              bool* closeConnection)
{
    *closeConnection = false;

    // 决定是否关闭连接
    const std::string& connectionHeader = req.getHeader("connection");
    bool close = false;

    if (req.version() == "HTTP/1.0")
    {
        // HTTP/1.0 默认关闭，除非 Connection: Keep-Alive
        close = (connectionHeader != "keep-alive");
    }
    else // HTTP/1.1 或更高
    {
        // HTTP/1.1 默认keep-alive，除非 Connection: close
        close = (connectionHeader == "close");
    }

    HttpResponse response(close);
    response.setStatusCode(HttpResponse::k200Ok);
    HttpRequest request(req);
    const HttpDispatcher::Result result = dispatcher_.dispatch(
        request, &response, asyncResponderFactory(conn, close));
    if (result == HttpDispatcher::kAsyncPending)
    {
        return result;
    }

    sendResponse(conn, response);
    *closeConnection = response.closeConnection();
    return result;
}

HttpDispatcher::AsyncResponderFactory HttpServer::asyncResponderFactory(
    const TcpConnectionPtr& conn, bool closeConnection)
{
    const std::weak_ptr<TcpConnection> weakConnection(conn);
    EventLoop* const loop = conn->getLoop();
    return [weakConnection, loop, closeConnection](
        const HttpRequest&,
        const std::vector<std::shared_ptr<Middleware> >&,
        const HttpDispatcher::AsyncCompletion& completion) {
        return AsyncResponder([weakConnection, loop, closeConnection, completion](
            HttpResponse asyncResponse) {
            loop->queueInLoop([weakConnection, closeConnection, completion, asyncResponse]() mutable {
                TcpConnectionPtr connection = weakConnection.lock();
                if (!connection || !connection->connected())
                {
                    return;
                }

                HttpResponse completed(asyncResponse);
                if (!completion(&completed))
                {
                    return;
                }
                if (closeConnection)
                {
                    completed.setCloseConnection(true);
                }

                HttpServer::sendResponse(connection, completed);
            });
        });
    };
}

void HttpServer::sendResponse(const TcpConnectionPtr& conn, HttpResponse response)
{
    int fileDescriptor = -1;
    if (response.hasFile())
    {
        fileDescriptor = ::open(response.filePath().c_str(), O_RDONLY | O_CLOEXEC);
        if (fileDescriptor < 0)
        {
            response = HttpResponse::makeErrorResponse(HttpResponse::k404NotFound,
                                                       response.closeConnection());
        }
    }

    Buffer output;
    response.appendToBuffer(&output);
    conn->send(output.retrieveAllAsString());
    if (fileDescriptor >= 0)
    {
        conn->sendFile(fileDescriptor, response.fileOffset(), response.fileCount());
    }
    if (response.closeConnection())
    {
        conn->shutdown();
    }
}

bool HttpServer::shouldContinueParsing(HttpDispatcher::Result result,
                                       bool closeConnection)
{
    return result == HttpDispatcher::kComplete && !closeConnection;
}

void HttpServer::sendParseError(const TcpConnectionPtr& conn,
                                HttpContext::ParseError error)
{
    HttpResponse::HttpStatusCode status = HttpResponse::k400BadRequest;
    if (error == HttpContext::kRequestLineTooLarge ||
        error == HttpContext::kHeadersTooLarge ||
        error == HttpContext::kBodyTooLarge)
    {
        status = HttpResponse::k413PayloadTooLarge;
    }
    else if (error == HttpContext::kUnsupportedTransferEncoding)
    {
        status = HttpResponse::k501NotImplemented;
    }

    LOG_WARN << "HTTP parse error from " << conn->peerAddress().toIpPort();
    HttpResponse response = HttpResponse::makeErrorResponse(
        status, true, "Failed to parse request");
    Buffer responseBuffer;
    response.appendToBuffer(&responseBuffer);
    conn->send(responseBuffer.retrieveAllAsString());
    conn->shutdown();
}
