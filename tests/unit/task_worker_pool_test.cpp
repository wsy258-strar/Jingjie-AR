#include "TestSupport.h"

#include <base/TaskWorkerPool.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {

void testPoolRejectsWhenBoundedQueueIsFullAndDrainsAcceptedTasks()
{
    TaskWorkerPool pool(1, 1);
    std::mutex mutex;
    std::condition_variable condition;
    bool started = false;
    bool release = false;
    std::atomic<int> ran(0);

    CHECK(pool.submit([&] {
        {
            std::lock_guard<std::mutex> lock(mutex);
            started = true;
        }
        condition.notify_one();
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return release; });
        ++ran;
    }));

    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return started; });
    }
    CHECK(pool.submit([&] { ++ran; }));
    CHECK(pool.pendingCount() == 1);
    CHECK(!pool.submit([&] { ++ran; }));

    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    condition.notify_one();

    for (int attempt = 0; attempt < 100 && ran.load() != 2; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(ran.load() == 2);
}

void testZeroWorkerPoolRejectsSubmissions()
{
    TaskWorkerPool pool(0, 1);
    CHECK(!pool.submit([] {}));
    CHECK(pool.pendingCount() == 0);
}

} // namespace

int main()
{
    testPoolRejectsWhenBoundedQueueIsFullAndDrainsAcceptedTasks();
    testZeroWorkerPoolRejectsSubmissions();
    return 0;
}
