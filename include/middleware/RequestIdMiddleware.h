#pragma once

#include <middleware/Middleware.h>

#include <atomic>
#include <cstdint>

class RequestIdMiddleware : public Middleware
{
public:
    RequestIdMiddleware();
    bool before(HttpRequest& request, HttpResponse& response) override;
    void after(const HttpRequest& request, HttpResponse& response) override;
private:
    uint64_t prefix_;
    std::atomic<uint64_t> counter_;
};
