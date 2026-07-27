// 数据库工作线程实现：工作线程独占借来的连接并在任务结束后归还。
#include "db/DBWorkerPool.h"
#include "db/MySQLConnectionPool.h"
#include "Logger.h"

DBWorkerPool::DBWorkerPool(MySQLConnectionPool* connPool, size_t workerCount)
    : connPool_(connPool)
{
    for (size_t i = 0; i < workerCount; ++i)
    {
        workers_.emplace_back(&DBWorkerPool::run, this);
    }
    LOG_INFO << "DBWorkerPool started with " << workerCount << " workers";
}

DBWorkerPool::~DBWorkerPool()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cond_.notify_all();

    for (auto& w : workers_)
    {
        if (w.joinable()) w.join();
    }
}

bool DBWorkerPool::submit(DBTask task)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_)
        {
            return false;
        }
        tasks_.push(std::move(task));
    }
    cond_.notify_one();
    return true;
}

size_t DBWorkerPool::pendingCount()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void DBWorkerPool::run()
{
    while (true)
    {
        DBTask task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait(lock, [this] {
                return !tasks_.empty() || !running_;
            });

            if (!running_ && tasks_.empty()) return;

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        // 借连接 → 执行 → 自动归还（shared_ptr deleter）
        std::shared_ptr<MYSQL> conn;
        if (connPool_)
        {
            conn = connPool_->borrow();
        }
        try
        {
            task(conn);
        }
        catch (const std::exception& error)
        {
            LOG_ERROR << "DBWorker task failed: " << error.what();
        }
        catch (...)
        {
            LOG_ERROR << "DBWorker task failed with unknown exception";
        }
        // conn 析构 → recycle() 归还
    }
}
