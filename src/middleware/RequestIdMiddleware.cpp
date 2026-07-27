// 请求 ID 生成与写入响应头实现。
#include <middleware/RequestIdMiddleware.h>

#include <iomanip>
#include <random>
#include <sstream>

RequestIdMiddleware::RequestIdMiddleware()
    : prefix_(0), counter_(0)
{
    std::random_device random;
    prefix_ = (static_cast<uint64_t>(random()) << 32) | static_cast<uint64_t>(random());
}

bool RequestIdMiddleware::before(HttpRequest& request, HttpResponse&)
{
    std::ostringstream output;
    output << std::hex << std::nouppercase << prefix_ << counter_.fetch_add(1);
    request.setAttribute("request_id", output.str());
    return true;
}

void RequestIdMiddleware::after(const HttpRequest& request, HttpResponse& response)
{
    response.addHeader("X-Request-Id", request.attribute("request_id"));
}
