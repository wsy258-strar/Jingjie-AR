#include "TestSupport.h"

#include <http/AsyncResponder.h>
#include <http/HttpDispatcher.h>

#include <memory>
#include <string>
#include <vector>

namespace {

class RecordingMiddleware : public Middleware
{
public:
    RecordingMiddleware(const std::string& name, std::vector<std::string>* events)
        : name_(name), events_(events)
    {}

    bool before(HttpRequest&, HttpResponse&) override
    {
        events_->push_back(name_ + ".before");
        return true;
    }

    void after(const HttpRequest&, HttpResponse&) override
    {
        events_->push_back(name_ + ".after");
    }

private:
    std::string name_;
    std::vector<std::string>* events_;
};

class ThrowingAfterMiddleware : public Middleware
{
public:
    ThrowingAfterMiddleware(const std::string& name, std::vector<std::string>* events)
        : name_(name), events_(events)
    {}

    bool before(HttpRequest&, HttpResponse&) override
    {
        events_->push_back(name_ + ".before");
        return true;
    }

    void after(const HttpRequest&, HttpResponse&) override
    {
        events_->push_back(name_ + ".after");
        throw std::runtime_error("after failed");
    }

private:
    std::string name_;
    std::vector<std::string>* events_;
};

class ThrowingIntegerAfterMiddleware : public Middleware
{
public:
    ThrowingIntegerAfterMiddleware(const std::string& name, std::vector<std::string>* events)
        : name_(name), events_(events)
    {}

    bool before(HttpRequest&, HttpResponse&) override
    {
        events_->push_back(name_ + ".before");
        return true;
    }

    void after(const HttpRequest&, HttpResponse&) override
    {
        events_->push_back(name_ + ".after");
        throw 42;
    }

private:
    std::string name_;
    std::vector<std::string>* events_;
};

HttpRequest request(HttpRequest::Method method, const std::string& path)
{
    HttpRequest value;
    value.setMethod(method);
    value.setPath(path);
    return value;
}

void testDispatchRunsRouteAndMiddlewareInNestingOrder()
{
    std::vector<std::string> events;
    HttpDispatcher dispatcher;
    CHECK(dispatcher.Get("/hello", [](const HttpRequest&, HttpResponse* response) {
        response->setBody("hello");
    }));
    dispatcher.addMiddleware(std::shared_ptr<Middleware>(
        new RecordingMiddleware("first", &events)));
    dispatcher.addMiddleware(std::shared_ptr<Middleware>(
        new RecordingMiddleware("second", &events)));

    HttpRequest incoming = request(HttpRequest::kGet, "/hello");
    HttpResponse response(false);
    CHECK(dispatcher.dispatch(incoming, &response) == HttpDispatcher::kComplete);
    CHECK(response.statusCode() == HttpResponse::k200Ok);
    CHECK(response.body() == "hello");
    CHECK(events == std::vector<std::string>({"first.before", "second.before",
                                               "second.after", "first.after"}));
}

void testUnknownRouteReturnsNotFound()
{
    HttpDispatcher dispatcher;
    HttpRequest incoming = request(HttpRequest::kGet, "/missing");
    HttpResponse response(false);

    CHECK(dispatcher.dispatch(incoming, &response) == HttpDispatcher::kComplete);
    CHECK(response.statusCode() == HttpResponse::k404NotFound);
}

void testUnsupportedMethodReturnsAllowHeader()
{
    HttpDispatcher dispatcher;
    CHECK(dispatcher.Get("/hello", [](const HttpRequest&, HttpResponse*) {}));
    HttpRequest incoming = request(HttpRequest::kPost, "/hello");
    HttpResponse response(false);

    CHECK(dispatcher.dispatch(incoming, &response) == HttpDispatcher::kComplete);
    CHECK(response.statusCode() == HttpResponse::k405MethodNotAllowed);
    CHECK(response.header("Allow") == "GET");
}

void testOptionsRouteIsDispatched()
{
    HttpDispatcher dispatcher;
    CHECK(dispatcher.Options("/hello", [](const HttpRequest&, HttpResponse* response) {
        response->setStatusCode(HttpResponse::k204NoContent);
    }));
    HttpRequest incoming = request(HttpRequest::kOptions, "/hello");
    HttpResponse response(false);

    CHECK(dispatcher.dispatch(incoming, &response) == HttpDispatcher::kComplete);
    CHECK(response.statusCode() == HttpResponse::k204NoContent);
}

void testAfterFailureReturnsInternalServerErrorAfterFullUnwind()
{
    std::vector<std::string> events;
    HttpDispatcher dispatcher;
    dispatcher.addMiddleware(std::shared_ptr<Middleware>(
        new RecordingMiddleware("first", &events)));
    dispatcher.addMiddleware(std::shared_ptr<Middleware>(
        new ThrowingAfterMiddleware("second", &events)));
    dispatcher.addMiddleware(std::shared_ptr<Middleware>(
        new RecordingMiddleware("third", &events)));

    HttpRequest incoming = request(HttpRequest::kGet, "/missing");
    HttpResponse response(false);
    CHECK(dispatcher.dispatch(incoming, &response) == HttpDispatcher::kComplete);
    CHECK(response.statusCode() == HttpResponse::k500InternalServerError);
    CHECK(events == std::vector<std::string>({"first.before", "second.before",
                                               "third.before", "third.after",
                                               "second.after", "first.after"}));
}

void testNonStandardAfterFailureReturnsInternalServerErrorAfterFullUnwind()
{
    std::vector<std::string> events;
    HttpDispatcher dispatcher;
    dispatcher.addMiddleware(std::shared_ptr<Middleware>(
        new RecordingMiddleware("first", &events)));
    dispatcher.addMiddleware(std::shared_ptr<Middleware>(
        new ThrowingIntegerAfterMiddleware("second", &events)));
    dispatcher.addMiddleware(std::shared_ptr<Middleware>(
        new RecordingMiddleware("third", &events)));

    HttpRequest incoming = request(HttpRequest::kGet, "/missing");
    HttpResponse response(false);
    bool escaped = false;
    try
    {
        CHECK(dispatcher.dispatch(incoming, &response) == HttpDispatcher::kComplete);
    }
    catch (...)
    {
        escaped = true;
    }

    CHECK(events == std::vector<std::string>({"first.before", "second.before",
                                               "third.before", "third.after",
                                               "second.after", "first.after"}));
    CHECK(!escaped);
    CHECK(response.statusCode() == HttpResponse::k500InternalServerError);
}

void testAsyncDispatchDefersAfterUntilResponderCompletes()
{
    std::vector<std::string> events;
    AsyncResponder storedResponder;
    HttpDispatcher dispatcher;
    CHECK(dispatcher.GetAsync("/async", [&storedResponder](const HttpRequest&,
                                                            const AsyncResponder& responder) {
        storedResponder = responder;
    }));
    dispatcher.addMiddleware(std::shared_ptr<Middleware>(
        new RecordingMiddleware("first", &events)));
    dispatcher.addMiddleware(std::shared_ptr<Middleware>(
        new RecordingMiddleware("second", &events)));

    HttpRequest incoming = request(HttpRequest::kGet, "/async");
    HttpResponse response(false);
    CHECK(dispatcher.dispatch(incoming, &response) == HttpDispatcher::kAsyncPending);
    CHECK(events == std::vector<std::string>({"first.before", "second.before"}));

    HttpResponse asyncResponse(false);
    asyncResponse.setBody("async");
    CHECK(storedResponder.send(asyncResponse));
    CHECK(events == std::vector<std::string>({"first.before", "second.before",
                                               "second.after", "first.after"}));
    CHECK(!storedResponder.send(asyncResponse));
}

void testAsyncCompletionExpiresWithDispatcher()
{
    HttpDispatcher::AsyncCompletion completion;
    {
        std::shared_ptr<HttpDispatcher> dispatcher(new HttpDispatcher());
        CHECK(dispatcher->GetAsync("/async", [](const HttpRequest&, const AsyncResponder&) {}));

        HttpRequest incoming = request(HttpRequest::kGet, "/async");
        HttpResponse response(false);
        CHECK(dispatcher->dispatch(
                  incoming, &response,
                  [&completion](const HttpRequest&,
                                const std::vector<std::shared_ptr<Middleware> >&,
                                const HttpDispatcher::AsyncCompletion& candidate) {
                      completion = candidate;
                      return AsyncResponder();
                  }) == HttpDispatcher::kAsyncPending);
    }

    HttpResponse asyncResponse(false);
    CHECK(!completion(&asyncResponse));
}

void testDeleteAsyncDispatchesAndCompletes()
{
    AsyncResponder storedResponder;
    HttpDispatcher dispatcher;
    CHECK(dispatcher.DeleteAsync("/api/scenes/:sceneId/likes",
                                 [&storedResponder](const HttpRequest&, const AsyncResponder& responder) {
        storedResponder = responder;
    }));
    HttpRequest incoming = request(HttpRequest::kDelete, "/api/scenes/golden-bay/likes");
    HttpResponse response(false);
    CHECK(dispatcher.dispatch(incoming, &response) == HttpDispatcher::kAsyncPending);
    HttpResponse completed(false);
    completed.setStatusCode(HttpResponse::k204NoContent);
    CHECK(storedResponder.send(completed));
}

} // namespace

int main()
{
    testDispatchRunsRouteAndMiddlewareInNestingOrder();
    testUnknownRouteReturnsNotFound();
    testUnsupportedMethodReturnsAllowHeader();
    testOptionsRouteIsDispatched();
    testAfterFailureReturnsInternalServerErrorAfterFullUnwind();
    testNonStandardAfterFailureReturnsInternalServerErrorAfterFullUnwind();
    testAsyncDispatchDefersAfterUntilResponderCompletes();
    testAsyncCompletionExpiresWithDispatcher();
    testDeleteAsyncDispatchesAndCompletes();
    return 0;
}
