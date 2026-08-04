#include "TestSupport.h"

#include <http/HttpRequest.h>
#include <http/HttpResponse.h>
#include <middleware/MiddlewareChain.h>
#include <middleware/cors/CorsConfig.h>
#include <middleware/cors/CorsMiddleware.h>

#include <memory>
#include <string>
#include <vector>

namespace {

class RecordingMiddleware : public Middleware
{
public:
    RecordingMiddleware(const std::string& name, std::vector<std::string>* events,
                        bool continueRequest = true)
        : name_(name), events_(events), continueRequest_(continueRequest)
    {}

    bool before(HttpRequest&, HttpResponse&) override
    {
        events_->push_back(name_ + ".before");
        return continueRequest_;
    }

    void after(const HttpRequest&, HttpResponse&) override
    {
        events_->push_back(name_ + ".after");
    }

private:
    std::string name_;
    std::vector<std::string>* events_;
    bool continueRequest_;
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

CorsConfig standardCorsConfig()
{
    CorsConfig config;
    config.allowedOrigins.push_back("https://app.example");
    config.allowedMethods.push_back("GET");
    config.allowedMethods.push_back("POST");
    config.allowedHeaders.push_back("Content-Type");
    config.maxAge = 600;
    return config;
}

void testMiddlewareRunsAfterInReverseOrder()
{
    std::vector<std::string> events;
    MiddlewareChain chain;
    chain.add(std::shared_ptr<Middleware>(new RecordingMiddleware("a", &events)));
    chain.add(std::shared_ptr<Middleware>(new RecordingMiddleware("b", &events)));

    HttpRequest request;
    HttpResponse response(false);
    std::vector<std::shared_ptr<Middleware> > executed;

    CHECK(chain.processBefore(request, response, executed));
    chain.processAfter(request, response, executed);
    CHECK(events == std::vector<std::string>({"a.before", "b.before",
                                               "b.after", "a.after"}));
}

void testStoppingMiddlewareStillRunsItsAfterAndSkipsLaterMiddleware()
{
    std::vector<std::string> events;
    MiddlewareChain chain;
    chain.add(std::shared_ptr<Middleware>(new RecordingMiddleware("a", &events)));
    chain.add(std::shared_ptr<Middleware>(new RecordingMiddleware("stop", &events, false)));
    chain.add(std::shared_ptr<Middleware>(new RecordingMiddleware("later", &events)));

    HttpRequest request;
    HttpResponse response(false);
    std::vector<std::shared_ptr<Middleware> > executed;

    CHECK(!chain.processBefore(request, response, executed));
    CHECK(executed.size() == 2);
    chain.processAfter(request, response, executed);
    CHECK(events == std::vector<std::string>({"a.before", "stop.before",
                                               "stop.after", "a.after"}));
}

void testAfterAttemptsEveryExecutedMiddlewareWhenOneThrows()
{
    std::vector<std::string> events;
    MiddlewareChain chain;
    chain.add(std::shared_ptr<Middleware>(new RecordingMiddleware("first", &events)));
    chain.add(std::shared_ptr<Middleware>(new ThrowingAfterMiddleware("second", &events)));
    chain.add(std::shared_ptr<Middleware>(new RecordingMiddleware("third", &events)));

    HttpRequest request;
    HttpResponse response(false);
    std::vector<std::shared_ptr<Middleware> > executed;
    CHECK(chain.processBefore(request, response, executed));

    bool threw = false;
    try
    {
        chain.processAfter(request, response, executed);
    }
    catch (const std::exception&)
    {
        threw = true;
    }

    CHECK(threw);
    CHECK(events == std::vector<std::string>({"first.before", "second.before",
                                               "third.before", "third.after",
                                               "second.after", "first.after"}));
}

void testValidPreflightReturnsNoContent()
{
    CorsMiddleware cors(standardCorsConfig());
    HttpRequest request;
    request.setMethod(HttpRequest::kOptions);
    request.addHeader("Origin", "https://app.example");
    request.addHeader("Access-Control-Request-Method", "POST");
    request.addHeader("Access-Control-Request-Headers", "Content-Type");
    HttpResponse response(false);

    CHECK(!cors.before(request, response));
    CHECK(response.statusCode() == HttpResponse::k204NoContent);
    CHECK(response.header("Access-Control-Allow-Origin") == "https://app.example");
    CHECK(response.header("Access-Control-Allow-Methods") == "GET, POST");
    CHECK(response.header("Access-Control-Allow-Headers") == "Content-Type");
    CHECK(response.header("Access-Control-Max-Age") == "600");
    CHECK(response.header("Vary") == "Origin");
}

void testRejectedOriginReturnsForbidden()
{
    CorsMiddleware cors(standardCorsConfig());
    HttpRequest request;
    request.setMethod(HttpRequest::kOptions);
    request.addHeader("Origin", "https://untrusted.example");
    request.addHeader("Access-Control-Request-Method", "GET");
    HttpResponse response(false);

    CHECK(!cors.before(request, response));
    CHECK(response.statusCode() == HttpResponse::k403Forbidden);
    CHECK(response.header("Access-Control-Allow-Origin").empty());
}

void testWildcardIsSentOnlyWithoutCredentials()
{
    CorsConfig config;
    config.allowedOrigins.push_back("*");
    CorsMiddleware cors(config);
    HttpRequest request;
    request.setMethod(HttpRequest::kGet);
    request.addHeader("Origin", "https://any.example");
    HttpResponse response(false);

    CHECK(cors.before(request, response));
    cors.after(request, response);
    CHECK(response.header("Access-Control-Allow-Origin") == "*");
    CHECK(response.header("Access-Control-Allow-Credentials").empty());
    CHECK(response.header("Vary").empty());
}

void testCredentialedRequestsEchoOnlyValidatedOriginAndVary()
{
    CorsConfig config = standardCorsConfig();
    config.allowCredentials = true;
    CorsMiddleware cors(config);
    HttpRequest request;
    request.setMethod(HttpRequest::kGet);
    request.addHeader("Origin", "https://app.example");
    HttpResponse response(false);

    CHECK(cors.before(request, response));
    cors.after(request, response);
    CHECK(response.header("Access-Control-Allow-Origin") == "https://app.example");
    CHECK(response.header("Access-Control-Allow-Credentials") == "true");
    CHECK(response.header("Vary") == "Origin");
}

void testCredentialedRequestAddsOriginToExistingVaryTokenList()
{
    CorsConfig config = standardCorsConfig();
    config.allowCredentials = true;
    CorsMiddleware cors(config);
    HttpRequest request;
    request.setMethod(HttpRequest::kGet);
    request.addHeader("Origin", "https://app.example");
    HttpResponse response(false);
    response.addHeader("Vary", "X-Origin-Policy");

    CHECK(cors.before(request, response));
    cors.after(request, response);
    CHECK(response.header("Vary") == "X-Origin-Policy, Origin");
}

void testPreflightRejectsLowercaseRequestedMethod()
{
    CorsMiddleware cors(standardCorsConfig());
    HttpRequest request;
    request.setMethod(HttpRequest::kOptions);
    request.addHeader("Origin", "https://app.example");
    request.addHeader("Access-Control-Request-Method", "post");
    HttpResponse response(false);

    CHECK(!cors.before(request, response));
    CHECK(response.statusCode() == HttpResponse::k403Forbidden);
}

} // namespace

int main()
{
    testMiddlewareRunsAfterInReverseOrder();
    testStoppingMiddlewareStillRunsItsAfterAndSkipsLaterMiddleware();
    testAfterAttemptsEveryExecutedMiddlewareWhenOneThrows();
    testValidPreflightReturnsNoContent();
    testRejectedOriginReturnsForbidden();
    testWildcardIsSentOnlyWithoutCredentials();
    testCredentialedRequestsEchoOnlyValidatedOriginAndVary();
    testCredentialedRequestAddsOriginToExistingVaryTokenList();
    testPreflightRejectsLowercaseRequestedMethod();
    return 0;
}
