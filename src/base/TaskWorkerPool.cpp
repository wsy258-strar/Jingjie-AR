#include <base/TaskWorkerPool.h>

TaskWorkerPool::TaskWorkerPool(size_t workers, size_t capacity)
    : capacity_(capacity),
      accepting_(true)
{
    for (size_t index = 0; index < workers; ++index)
    {
        workers_.push_back(std::thread(&TaskWorkerPool::run, this));
    }
}

TaskWorkerPool::~TaskWorkerPool()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = false;
    }
    condition_.notify_all();
    for (std::vector<std::thread>::iterator it = workers_.begin(); it != workers_.end(); ++it)
    {
        if (it->joinable())
        {
            it->join();
        }
    }
}

bool TaskWorkerPool::submit(const Task& task)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!accepting_ || workers_.empty() || tasks_.size() >= capacity_)
    {
        return false;
    }
    tasks_.push(task);
    condition_.notify_one();
    return true;
}

size_t TaskWorkerPool::pendingCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void TaskWorkerPool::run()
{
    while (true)
    {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return !tasks_.empty() || !accepting_; });
            if (tasks_.empty())
            {
                return;
            }
            task = tasks_.front();
            tasks_.pop();
        }
        try
        {
            task();
        }
        catch (...)
        {
        }
    }
}
