#include "TestSupport.h"

#include <base/TaskWorkerPool.h>
#include <db/ExhibitionStatisticsDAO.h>
#include <handlers/VisitorHandlers.h>
#include <services/PresenceService.h>
#include <services/VisitorSessionService.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace {

const char* const kToken =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const char* const kRequestId = "page-550e8400-e29b-41d4-a716-446655440000";

class FakeVisitorStore : public ar::VisitorStore
{
public:
    FakeVisitorStore() : available(true), saveCalls(0) {}

    bool exists(const std::string& token) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return available && token == kToken && saved;
    }

    bool save(const std::string& token, int) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!available || token != kToken) return false;
        ++saveCalls;
        saved = true;
        return true;
    }

    bool claimBootstrap(const std::string& requestId, const std::string& candidateToken,
                        int, std::string* resolvedToken) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!available || !resolvedToken) return false;
        if (claimedIds.insert(requestId).second)
        {
            tokens[requestId] = candidateToken;
        }
        *resolvedToken = tokens[requestId];
        return true;
    }

    bool available;
    bool saved = false;
    int saveCalls;
    std::mutex mutex;
    std::set<std::string> claimedIds;
    std::map<std::string, std::string> tokens;
};

class FakePresenceStore : public ar::PresenceStore
{
public:
    FakePresenceStore() : touchOk(true), removeOk(true), countOk(true), online(2) {}
    bool touch(const std::string&, int64_t) override { return touchOk; }
    bool remove(const std::string&) override { return removeOk; }
    bool count(int64_t, uint64_t* value) override
    {
        if (!countOk || !value) return false;
        *value = online;
        return true;
    }
    bool touchOk;
    bool removeOk;
    bool countOk;
    uint64_t online;
};

class FakeStatisticsDAO : public ExhibitionStatisticsDAO
{
public:
    FakeStatisticsDAO() : ExhibitionStatisticsDAO(0), incrementCalls(0), readCalls(0),
                          available(true), failNextIncrement(false), count(1287) {}

    void incrementAndRead(const std::string&, const std::string& bootstrapRequestId,
                          const CountCallback& callback) override
    {
        ++incrementCalls;
        incrementRequestIds.push_back(bootstrapRequestId);
        if (failNextIncrement)
        {
            failNextIncrement = false;
            callback(false, 0);
            return;
        }
        if (countedRequestIds.insert(bootstrapRequestId).second) ++count;
        callback(available, available ? count : 0);
    }

    void read(const std::string&, const CountCallback& callback) override
    {
        ++readCalls;
        callback(available, available ? count : 0);
    }

    std::atomic<int> incrementCalls;
    std::atomic<int> readCalls;
    bool available;
    bool failNextIncrement;
    uint64_t count;
    std::vector<std::string> incrementRequestIds;
    std::set<std::string> countedRequestIds;
};

struct Capture
{
    Capture() : sent(false) {}
    std::atomic<bool> sent;
    HttpResponse response{false};
    std::mutex mutex;
};

HttpResponse waitFor(const std::function<void(const AsyncResponder&)>& invoke)
{
    std::shared_ptr<Capture> capture(new Capture);
    invoke(AsyncResponder([capture](HttpResponse response) {
        {
            std::lock_guard<std::mutex> lock(capture->mutex);
            capture->response = response;
        }
        capture->sent.store(true, std::memory_order_release);
    }));
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!capture->sent.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(capture->sent.load(std::memory_order_acquire));
    std::lock_guard<std::mutex> lock(capture->mutex);
    return capture->response;
}

HttpRequest bootstrapRequest(const std::string& requestId, bool withToken)
{
    HttpRequest request;
    request.setMethod(HttpRequest::kPost);
    request.addHeader("Content-Type", "application/json; charset=utf-8");
    if (withToken) request.addHeader("X-Visitor-Token", kToken);
    request.setBody(std::string("{\"bootstrapRequestId\":\"") + requestId + "\"}");
    request.setAttribute("request_id", "http-request-id");
    return request;
}

void testBootstrapIsIdempotentPerRequestIdButNewRequestIncrements()
{
    FakeVisitorStore visitors;
    FakePresenceStore presenceStore;
    FakeStatisticsDAO statistics;
    ar::VisitorSessionService sessions(&visitors, [] { return std::string(kToken); });
    ar::PresenceService presence(&presenceStore);
    TaskWorkerPool workers(1, 8);
    ar::VisitorHandlers handlers(&sessions, &presence, &statistics, &workers, "museum");

    HttpRequest first = bootstrapRequest(kRequestId, false);
    HttpResponse response = waitFor([&](const AsyncResponder& responder) {
        handlers.bootstrap(first, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k200Ok);
    CHECK(response.body().find(std::string("\"visitorToken\":\"") + kToken + "\"") !=
          std::string::npos);
    CHECK(response.body().find("\"totalViews\":1288") != std::string::npos);
    CHECK(response.body().find("\"statisticsAvailable\":true") != std::string::npos);
    CHECK(statistics.incrementCalls.load() == 1);

    HttpRequest retry = bootstrapRequest(kRequestId, true);
    response = waitFor([&](const AsyncResponder& responder) {
        handlers.bootstrap(retry, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k200Ok);
    CHECK(response.body().find(std::string("\"visitorToken\":\"") + kToken + "\"") !=
          std::string::npos);
    CHECK(response.body().find("\"totalViews\":1288") != std::string::npos);
    CHECK(statistics.incrementCalls.load() == 2);
    CHECK(statistics.incrementRequestIds.size() == 2);
    CHECK(statistics.incrementRequestIds[0] == kRequestId);
    CHECK(statistics.incrementRequestIds[1] == kRequestId);

    HttpRequest refresh = bootstrapRequest("page-new-request", true);
    response = waitFor([&](const AsyncResponder& responder) {
        handlers.bootstrap(refresh, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k200Ok);
    CHECK(statistics.incrementCalls.load() == 3);
    CHECK(response.body().find("\"totalViews\":1289") != std::string::npos);
}

void testBootstrapRetriesDatabaseAfterFailedTransactionWithSameRequestId()
{
    FakeVisitorStore visitors;
    FakePresenceStore presenceStore;
    FakeStatisticsDAO statistics;
    statistics.failNextIncrement = true;
    ar::VisitorSessionService sessions(&visitors, [] { return std::string(kToken); });
    ar::PresenceService presence(&presenceStore);
    TaskWorkerPool workers(1, 8);
    ar::VisitorHandlers handlers(&sessions, &presence, &statistics, &workers, "museum");

    HttpRequest request = bootstrapRequest("page-database-retry", false);
    HttpResponse first = waitFor([&](const AsyncResponder& responder) {
        handlers.bootstrap(request, responder);
    });
    CHECK(first.statusCode() == HttpResponse::k200Ok);
    CHECK(first.body().find("\"statisticsAvailable\":false") != std::string::npos);

    HttpRequest retry = bootstrapRequest("page-database-retry", true);
    HttpResponse second = waitFor([&](const AsyncResponder& responder) {
        handlers.bootstrap(retry, responder);
    });
    CHECK(second.statusCode() == HttpResponse::k200Ok);
    CHECK(second.body().find("\"statisticsAvailable\":true") != std::string::npos);
    CHECK(second.body().find("\"totalViews\":1288") != std::string::npos);
    CHECK(statistics.incrementCalls.load() == 2);
    CHECK(statistics.incrementRequestIds[0] == "page-database-retry");
    CHECK(statistics.incrementRequestIds[1] == "page-database-retry");
}

void testBootstrapValidationAndDependencyFailures()
{
    FakeVisitorStore visitors;
    FakePresenceStore presenceStore;
    FakeStatisticsDAO statistics;
    ar::VisitorSessionService sessions(&visitors, [] { return std::string(kToken); });
    ar::PresenceService presence(&presenceStore);
    TaskWorkerPool workers(1, 8);
    ar::VisitorHandlers handlers(&sessions, &presence, &statistics, &workers, "museum");

    HttpRequest malformed;
    malformed.setMethod(HttpRequest::kPost);
    malformed.addHeader("Content-Type", "application/json");
    malformed.setBody("{");
    malformed.setAttribute("request_id", "bad-json");
    HttpResponse response = waitFor([&](const AsyncResponder& responder) {
        handlers.bootstrap(malformed, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k400BadRequest);
    CHECK(response.body().find("\"code\":\"INVALID_JSON\"") != std::string::npos);

    visitors.available = false;
    HttpRequest request = bootstrapRequest(kRequestId, false);
    response = waitFor([&](const AsyncResponder& responder) {
        handlers.bootstrap(request, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k503ServiceUnavailable);

    visitors.available = true;
    statistics.available = false;
    request = bootstrapRequest("page-mysql-down", false);
    response = waitFor([&](const AsyncResponder& responder) {
        handlers.bootstrap(request, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k200Ok);
    CHECK(response.body().find("\"totalViews\":null") != std::string::npos);
    CHECK(response.body().find("\"statisticsAvailable\":false") != std::string::npos);
}

void testPresenceEndpointsRequireVisitorAndDegradeIndependently()
{
    FakeVisitorStore visitors;
    FakePresenceStore presenceStore;
    FakeStatisticsDAO statistics;
    ar::VisitorSessionService sessions(&visitors, [] { return std::string(kToken); });
    ar::PresenceService presence(&presenceStore);
    TaskWorkerPool workers(1, 8);
    ar::VisitorHandlers handlers(&sessions, &presence, &statistics, &workers, "museum");

    HttpRequest heartbeat;
    heartbeat.setAttribute("request_id", "heartbeat");
    HttpResponse response = waitFor([&](const AsyncResponder& responder) {
        handlers.heartbeat(heartbeat, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k401Unauthorized);
    CHECK(response.body().find("\"code\":\"VISITOR_TOKEN_REQUIRED\"") != std::string::npos);

    HttpRequest bootstrap = bootstrapRequest(kRequestId, false);
    (void)waitFor([&](const AsyncResponder& responder) {
        handlers.bootstrap(bootstrap, responder);
    });

    heartbeat.addHeader("X-Visitor-Token", kToken);
    visitors.saved = false;
    response = waitFor([&](const AsyncResponder& responder) {
        handlers.heartbeat(heartbeat, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k200Ok);
    CHECK(visitors.saved);

    presenceStore.touchOk = false;
    response = waitFor([&](const AsyncResponder& responder) {
        handlers.heartbeat(heartbeat, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k503ServiceUnavailable);

    presenceStore.touchOk = true;
    visitors.available = false;
    response = waitFor([&](const AsyncResponder& responder) {
        handlers.heartbeat(heartbeat, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k503ServiceUnavailable);
    visitors.available = true;

    HttpRequest current;
    current.setAttribute("request_id", "presence");
    response = waitFor([&](const AsyncResponder& responder) {
        handlers.presence(current, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k200Ok);
    CHECK(response.body().find("\"data\":{\"onlineCount\":2}") != std::string::npos);
    CHECK(response.body().find(kToken) == std::string::npos);

    presenceStore.countOk = false;
    response = waitFor([&](const AsyncResponder& responder) {
        handlers.presence(current, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k503ServiceUnavailable);
}

void testViewsAndExitResponses()
{
    FakeVisitorStore visitors;
    FakePresenceStore presenceStore;
    FakeStatisticsDAO statistics;
    ar::VisitorSessionService sessions(&visitors, [] { return std::string(kToken); });
    ar::PresenceService presence(&presenceStore);
    TaskWorkerPool workers(1, 8);
    ar::VisitorHandlers handlers(&sessions, &presence, &statistics, &workers, "museum");

    HttpRequest bootstrap = bootstrapRequest(kRequestId, false);
    (void)waitFor([&](const AsyncResponder& responder) {
        handlers.bootstrap(bootstrap, responder);
    });

    HttpRequest views;
    views.setAttribute("request_id", "views");
    HttpResponse response = waitFor([&](const AsyncResponder& responder) {
        handlers.views(views, responder);
    });
    CHECK(response.body().find("\"totalViews\":1288") != std::string::npos);

    HttpRequest exit;
    exit.addHeader("X-Visitor-Token", kToken);
    exit.setAttribute("request_id", "exit");
    response = waitFor([&](const AsyncResponder& responder) {
        handlers.exit(exit, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k200Ok);
}

} // namespace

int main()
{
    testBootstrapIsIdempotentPerRequestIdButNewRequestIncrements();
    testBootstrapRetriesDatabaseAfterFailedTransactionWithSameRequestId();
    testBootstrapValidationAndDependencyFailures();
    testPresenceEndpointsRequireVisitorAndDegradeIndependently();
    testViewsAndExitResponses();
    return 0;
}
