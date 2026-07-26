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
