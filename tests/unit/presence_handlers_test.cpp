#include "TestSupport.h"

#include <handlers/PresenceHandlers.h>
#include <services/PresenceService.h>
#include <services/SessionService.h>
#include <base/TaskWorkerPool.h>

#include <chrono>
#include <atomic>
#include <mutex>

class FakePresenceStore : public ar::PresenceStore
{
public:
    bool touch(const std::string&, int64_t) override { return true; }
    bool remove(const std::string&) override { return true; }
    bool count(int64_t, uint64_t* value) override
    {
        if (!value) return false;
        *value = 1;
        return true;
    }
    bool touch(const std::string&, const std::string&, int64_t) override { return true; }
    bool remove(const std::string&, const std::string&) override { return true; }
    bool active(const std::string&, int64_t cutoff, std::vector<ar::PresenceEntry>* entries) override
    {
        entries->push_back(ar::PresenceEntry("member-token", cutoff + 1));
        return true;
    }
};

class FakeSessionStore : public ar::SessionStore
{
public:
    void find(const std::string&, const SessionCallback& callback) override
    { callback(std::shared_ptr<Session>(new Session(7, "member-token", 42, "1", 1))); }
    void enter(uint64_t, const std::string&, const BoolCallback& callback) override { callback(true); }
    void exit(uint64_t, const BoolCallback& callback) override { callback(true); }
};

int main()
{
    ar::PresenceHandlers handlers(0, 0, 0);
    HttpRequest request;
    request.setQuery("token=token&scene=1");
    HttpResponse response(false);
    handlers.heartbeat(request, AsyncResponder([&response](HttpResponse value) { response = value; }));
    CHECK(response.statusCode() == HttpResponse::k503ServiceUnavailable);
    HttpRequest members;
    members.setPathParameter("sceneId", "1");
    response = HttpResponse(false);
    handlers.members(members, AsyncResponder([&response](HttpResponse value) { response = value; }));
    CHECK(response.statusCode() == HttpResponse::k503ServiceUnavailable);

    FakePresenceStore presenceStore;
    FakeSessionStore sessionStore;
    ar::PresenceService presence(&presenceStore);
    ar::SessionService sessions(&sessionStore);
    std::atomic<bool> sent(false);
    TaskWorkerPool workers(1, 2);
    ar::PresenceHandlers working(&presence, &sessions, &workers);
    working.members(members, AsyncResponder([&](HttpResponse value) {
        response = value;
        sent.store(true, std::memory_order_release);
    }));
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!sent.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(sent.load(std::memory_order_acquire));
    CHECK(response.body().find("\"member_id\":7") != std::string::npos);
    CHECK(response.body().find("\"user_id\":42") != std::string::npos);
    CHECK(response.body().find("member-token") == std::string::npos);
    return 0;
}
