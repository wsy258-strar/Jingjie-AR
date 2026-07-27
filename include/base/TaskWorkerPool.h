// 有界后台任务池：将可能阻塞的工作移出 I/O 事件循环，并以队列容量提供背压。
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

    /// 启动 workers 个工作线程；capacity 是允许排队但尚未执行的最大任务数。
    TaskWorkerPool(size_t workers, size_t capacity);
    ~TaskWorkerPool();

    /// 非阻塞提交任务；关闭、无工作线程或队列已满时返回 false。
    bool submit(const Task& task);
    /// 返回当前等待队列长度，不包含已被工作线程取走的任务。
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
