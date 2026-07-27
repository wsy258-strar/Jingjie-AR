// HTTP 分发器：协调中间件、路由匹配、回退处理器和异步响应的完成顺序。
#pragma once

#include <http/AsyncResponder.h>
#include <http/HttpRequest.h>
#include <http/HttpResponse.h>
#include <middleware/MiddlewareChain.h>
#include <router/Router.h>

#include <functional>
#include <memory>
#include <string>

class HttpDispatcher
{
public:
    /**
     * 同步回调在当前 I/O 线程完成响应；异步回调必须使用 AsyncResponder 恰好完成一次。
     * dispatch 返回 kAsyncPending 时，调用方不得复用该请求解析上下文直到响应完成。
     */
    typedef std::function<void(const HttpRequest&, HttpResponse*)> HttpCallback;
    typedef std::function<void(const HttpRequest&, const AsyncResponder&)> AsyncCallback;
    typedef std::function<bool(HttpResponse*)> AsyncCompletion;
    typedef std::function<AsyncResponder(
        const HttpRequest&,
        const std::vector<std::shared_ptr<Middleware> >&,
        const AsyncCompletion&)> AsyncResponderFactory;

    enum Result
    {
        kComplete,
        kAsyncPending
    };

    HttpDispatcher();

    bool Get(const std::string& pattern, const HttpCallback& callback);
    bool Post(const std::string& pattern, const HttpCallback& callback);
    bool Put(const std::string& pattern, const HttpCallback& callback);
    bool Delete(const std::string& pattern, const HttpCallback& callback);
    bool Options(const std::string& pattern, const HttpCallback& callback);
    bool GetAsync(const std::string& pattern, const AsyncCallback& callback);
    bool PostAsync(const std::string& pattern, const AsyncCallback& callback);
    bool DeleteAsync(const std::string& pattern, const AsyncCallback& callback);

    void addMiddleware(const std::shared_ptr<Middleware>& middleware);
    void setFallback(const HttpCallback& callback);
    void setAsyncFallback(const AsyncCallback& callback);

    Result dispatch(HttpRequest& request, HttpResponse* response,
                    const AsyncResponderFactory& asyncFactory = AsyncResponderFactory());

private:
    static std::string allowedMethods(const std::vector<HttpRequest::Method>& methods);
    static void setInternalError(HttpResponse* response);
    static void completeAsync(MiddlewareChain* middleware,
                              const HttpRequest& request, HttpResponse* response,
                              const std::vector<std::shared_ptr<Middleware> >& executed);

    http::router::Router router_;
    std::shared_ptr<MiddlewareChain> middleware_;
    HttpCallback fallback_;
    AsyncCallback asyncFallback_;
};
