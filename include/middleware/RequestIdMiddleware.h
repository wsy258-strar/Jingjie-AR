// 请求标识中间件：用进程前缀与原子递增计数生成可关联的请求 ID。
#pragma once

#include <middleware/Middleware.h>

#include <atomic>
#include <cstdint>

class RequestIdMiddleware : public Middleware
{
public:
    /// 计数器为原子变量，允许多个 I/O 线程并发生成唯一请求序号。
    RequestIdMiddleware();
    bool before(HttpRequest& request, HttpResponse& response) override;
    void after(const HttpRequest& request, HttpResponse& response) override;
private:
    uint64_t prefix_;
    std::atomic<uint64_t> counter_;
};
