// 请求分发实现：同步路径立即收尾，异步路径将 after 阶段延后至 responder 完成时。
#include <http/HttpDispatcher.h>

#include <exception>
#include <vector>

HttpDispatcher::HttpDispatcher()
    : middleware_(new MiddlewareChain())
{
}

bool HttpDispatcher::Get(const std::string& pattern, const HttpCallback& callback)
{
    return router_.add(HttpRequest::kGet, pattern, callback);
}

bool HttpDispatcher::Post(const std::string& pattern, const HttpCallback& callback)
{
    return router_.add(HttpRequest::kPost, pattern, callback);
}

bool HttpDispatcher::Put(const std::string& pattern, const HttpCallback& callback)
{
    return router_.add(HttpRequest::kPut, pattern, callback);
}

bool HttpDispatcher::Delete(const std::string& pattern, const HttpCallback& callback)
{
    return router_.add(HttpRequest::kDelete, pattern, callback);
}

bool HttpDispatcher::Options(const std::string& pattern, const HttpCallback& callback)
{
    return router_.add(HttpRequest::kOptions, pattern, callback);
}

bool HttpDispatcher::GetAsync(const std::string& pattern, const AsyncCallback& callback)
{
    return router_.addAsync(HttpRequest::kGet, pattern, callback);
}

bool HttpDispatcher::PostAsync(const std::string& pattern, const AsyncCallback& callback)
{
    return router_.addAsync(HttpRequest::kPost, pattern, callback);
}

bool HttpDispatcher::DeleteAsync(const std::string& pattern, const AsyncCallback& callback)
{
    return router_.addAsync(HttpRequest::kDelete, pattern, callback);
}

void HttpDispatcher::addMiddleware(const std::shared_ptr<Middleware>& middleware)
{
    middleware_->add(middleware);
}

void HttpDispatcher::setFallback(const HttpCallback& callback)
{
    fallback_ = callback;
}

void HttpDispatcher::setAsyncFallback(const AsyncCallback& callback)
{
    asyncFallback_ = callback;
}

HttpDispatcher::Result HttpDispatcher::dispatch(HttpRequest& request,
                                                 HttpResponse* response,
                                                 const AsyncResponderFactory& asyncFactory)
{
    const std::shared_ptr<MiddlewareChain> middleware = middleware_;
    std::vector<std::shared_ptr<Middleware> > executed;
    Result result = kComplete;

    try
    {
        if (middleware->processBefore(request, *response, executed))
        {
            std::vector<HttpRequest::Method> allowed;
            const HttpRequest requestCopy(request);
            const std::vector<std::shared_ptr<Middleware> > executedCopy(executed);
            const std::weak_ptr<MiddlewareChain> weakMiddleware(middleware);
            const AsyncCompletion completion =
                [weakMiddleware, requestCopy, executedCopy](HttpResponse* asyncResponse) {
                    const std::shared_ptr<MiddlewareChain> activeMiddleware =
                        weakMiddleware.lock();
                    if (!activeMiddleware)
                    {
                        return false;
                    }
                    completeAsync(activeMiddleware.get(), requestCopy, asyncResponse, executedCopy);
                    return true;
                };
            AsyncResponder responder = asyncFactory
                ? asyncFactory(requestCopy, executedCopy, completion)
                : AsyncResponder([completion](HttpResponse asyncResponse) {
                    completion(&asyncResponse);
                });
            const http::router::MatchResult routeResult =
                router_.route(request, response, &allowed, &responder);
            if (routeResult == http::router::kAsyncPending)
            {
                result = kAsyncPending;
            }
            else if (routeResult == http::router::kNotFound)
            {
                if (asyncFallback_)
                {
                    asyncFallback_(request, responder);
                    result = kAsyncPending;
                }
                else if (fallback_)
                {
                    fallback_(request, response);
                }
                else
                {
                    response->setStatusCode(HttpResponse::k404NotFound);
                }
            }
            else if (routeResult == http::router::kMethodNotAllowed)
            {
                response->setStatusCode(HttpResponse::k405MethodNotAllowed);
                response->addHeader("Allow", allowedMethods(allowed));
            }
        }
    }
    catch (...)
    {
        setInternalError(response);
        result = kComplete;
    }

    if (result != kAsyncPending)
    {
        completeAsync(middleware.get(), request, response, executed);
    }

    return result;
}

void HttpDispatcher::completeAsync(
    MiddlewareChain* middleware, const HttpRequest& request, HttpResponse* response,
    const std::vector<std::shared_ptr<Middleware> >& executed)
{
    try
    {
        middleware->processAfter(request, *response, executed);
    }
    catch (...)
    {
        setInternalError(response);
    }
}

std::string HttpDispatcher::allowedMethods(
    const std::vector<HttpRequest::Method>& methods)
{
    std::string value;
    for (std::vector<HttpRequest::Method>::const_iterator it = methods.begin();
         it != methods.end(); ++it)
    {
        if (!value.empty())
        {
            value += ", ";
        }
        value += HttpRequest::methodString(*it);
    }
    return value;
}

void HttpDispatcher::setInternalError(HttpResponse* response)
{
    response->setStatusCode(HttpResponse::k500InternalServerError);
    response->setContentType("application/json");
    response->setBody("{\"error\":\"Internal Server Error\"}");
}
