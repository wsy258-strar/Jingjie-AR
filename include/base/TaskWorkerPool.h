#pragma once

#include <base/noncopyable.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class TaskWorkerPool : noncopyable
{
public:
    typedef std::function<void()> Task;

    TaskWorkerPool(size_t workers, size_t capacity);
    ~TaskWorkerPool();

    bool submit(const Task& task);
    size_t pendingCount() const;

private:
    void run();

    const size_t capacity_;
    bool accepting_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<Task> tasks_;
    std::vector<std::thread> workers_;
};
