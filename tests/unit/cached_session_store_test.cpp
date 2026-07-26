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
    std::atomic<int> finds;
};

class FakeCache : public ar::SessionCachePort
{
public:
    FakeCache() : hasValue(false), puts(0), removes(0) {}
    bool get(const std::string&, Session& output) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (!hasValue) return false;
        output = value;
        return true;
    }
    bool put(const Session& input) override {
        { std::lock_guard<std::mutex> lock(mutex); value = input; hasValue = true; ++puts; }
        putDone.notify_one();
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
    bool waitForPut() {
        std::unique_lock<std::mutex> lock(mutex);
        return putDone.wait_for(lock, std::chrono::seconds(1), [this] { return puts.load() != 0; });
    }
private:
    std::mutex mutex;
    std::condition_variable putDone;
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
    return 0;
}
