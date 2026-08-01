#include "TestSupport.h"

#include <services/CachedSessionStore.h>
#include <base/TaskWorkerPool.h>

#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>

class FakeDurableStore : public ar::SessionStore
{
public:
    FakeDurableStore() : finds(0) {}
    void find(const std::string& token, const SessionCallback& callback) override
    { ++finds; callback(std::shared_ptr<Session>(new Session(8, token, 9, "1", 1))); }
    void enter(uint64_t, const std::string&, const BoolCallback& callback) override { callback(true); }
    void exit(uint64_t, const BoolCallback& callback) override { callback(true); }
    void revoke(const std::string&, const BoolCallback& callback) override { callback(true); }
    std::atomic<int> finds;
};

class FakeCache : public ar::SessionCachePort
{
public:
    FakeCache() : hasValue(false), puts(0), removes(0), putSucceeds(true) {}
    bool get(const std::string&, Session& output) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (!hasValue) return false;
        output = value;
        return true;
    }
    bool put(const Session& input) override {
        if (!putSucceeds) return false;
        { std::lock_guard<std::mutex> lock(mutex); value = input; hasValue = true; ++puts; }
        putDone.notify_one();
        return true;
    }
    bool putIfAbsent(const Session& input) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (hasValue) return false;
        value = input;
        hasValue = true;
        ++puts;
        return true;
    }
    bool remove(const std::string&) override {
        std::lock_guard<std::mutex> lock(mutex);
        hasValue = false;
        ++removes;
        return true;
    }
    bool hasValue;
    Session value;
    std::atomic<int> puts;
    std::atomic<int> removes;
    bool putSucceeds;
    bool waitForPut() {
        std::unique_lock<std::mutex> lock(mutex);
        return putDone.wait_for(lock, std::chrono::seconds(1), [this] { return puts.load() != 0; });
    }
private:
    std::mutex mutex;
    std::condition_variable putDone;
};

class DelayedDurableStore : public ar::SessionStore
{
public:
    DelayedDurableStore() : pendingReady(false) {}
    void find(const std::string&, const SessionCallback& callback) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            pending = callback;
            pendingReady = true;
        }
        condition.notify_all();
    }
    void enter(uint64_t, const std::string&, const BoolCallback& callback) override
    { callback(false); }
    void exit(uint64_t, const BoolCallback& callback) override { callback(false); }
    void revoke(const std::string&, const BoolCallback& callback) override { callback(true); }
    void waitForPending()
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(condition.wait_for(lock, std::chrono::seconds(1), [this] { return pendingReady; }));
    }
    void completeActive()
    {
        SessionCallback callback;
        {
            std::lock_guard<std::mutex> lock(mutex);
            callback = pending;
        }
        callback(std::shared_ptr<Session>(new Session(8, "race-token", 9, "", 1)));
    }
private:
    std::mutex mutex;
    std::condition_variable condition;
    SessionCallback pending;
    bool pendingReady;
};

int main()
{
    FakeDurableStore durable;
    FakeCache cache;
    std::atomic<bool> completed(false);
    TaskWorkerPool workers(1, 8);
    ar::CachedSessionStore store(&durable, &cache, &workers);
    store.find("token", [&](const std::shared_ptr<Session>& session) {
        CHECK(session && session->id == 8);
        completed.store(true, std::memory_order_release);
    });
    const std::chrono::steady_clock::time_point firstDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!completed.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < firstDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(completed.load(std::memory_order_acquire));
    CHECK(durable.finds.load() == 1);
    CHECK(cache.waitForPut());
    CHECK(cache.puts == 1);
    completed.store(false, std::memory_order_release);
    store.find("token", [&](const std::shared_ptr<Session>& session) {
        CHECK(session && session->id == 8);
        completed.store(true, std::memory_order_release);
    });
    const std::chrono::steady_clock::time_point secondDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!completed.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < secondDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(completed.load(std::memory_order_acquire));
    CHECK(durable.finds.load() == 1);
    std::atomic<bool> revoked(false);
    store.revoke("token", [&](bool ok) { CHECK(ok); revoked.store(true); });
    const std::chrono::steady_clock::time_point revokeDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!revoked.load() && std::chrono::steady_clock::now() < revokeDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(revoked.load());
    CHECK(cache.puts == 2);
    CHECK(cache.value.status == 0);
    cache.putSucceeds = false;
    std::atomic<bool> failedRevokeCompleted(false);
    store.revoke("token", [&](bool ok) {
        CHECK(!ok);
        failedRevokeCompleted.store(true);
    });
    const std::chrono::steady_clock::time_point failedRevokeDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!failedRevokeCompleted.load() &&
           std::chrono::steady_clock::now() < failedRevokeDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(failedRevokeCompleted.load());

    FakeCache raceCache;
    DelayedDurableStore delayedDurable;
    TaskWorkerPool raceWorkers(1, 8);
    ar::CachedSessionStore raceStore(&delayedDurable, &raceCache, &raceWorkers);
    std::shared_ptr<Session> raceResult;
    std::atomic<bool> raceFindCompleted(false);
    raceStore.find("race-token", [&](const std::shared_ptr<Session>& session) {
        raceResult = session;
        raceFindCompleted.store(true);
    });
    delayedDurable.waitForPending();
    std::atomic<bool> raceRevokeCompleted(false);
    raceStore.revoke("race-token", [&](bool ok) {
        CHECK(ok);
        raceRevokeCompleted.store(true);
    });
    const std::chrono::steady_clock::time_point raceRevokeDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!raceRevokeCompleted.load() &&
           std::chrono::steady_clock::now() < raceRevokeDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(raceRevokeCompleted.load());
    CHECK(raceCache.hasValue && raceCache.value.status == 0);
    delayedDurable.completeActive();
    const std::chrono::steady_clock::time_point raceFindDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!raceFindCompleted.load() &&
           std::chrono::steady_clock::now() < raceFindDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(raceFindCompleted.load());
    CHECK(raceResult && raceResult->status == 0);
    CHECK(raceCache.value.status == 0);

    FakeCache shutdownCache;
    DelayedDurableStore shutdownDurable;
    TaskWorkerPool shutdownWorkers(1, 8);
    ar::CachedSessionStore shutdownStore(
        &shutdownDurable, &shutdownCache, &shutdownWorkers);
    std::atomic<bool> shutdownFindCompleted(false);
    std::shared_ptr<Session> shutdownResult(new Session());
    shutdownStore.find("shutdown-token", [&](const std::shared_ptr<Session>& session) {
        shutdownResult = session;
        shutdownFindCompleted.store(true);
    });
    shutdownDurable.waitForPending();
    shutdownWorkers.shutdown();
    shutdownDurable.completeActive();
    CHECK(shutdownFindCompleted.load());
    CHECK(!shutdownResult);
    shutdownWorkers.shutdown();
    return 0;
}
