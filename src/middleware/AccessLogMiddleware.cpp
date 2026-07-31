// 访问日志实现：日志目标一律丢弃查询参数，业务处理仍使用原始请求。
#include <middleware/AccessLogMiddleware.h>

#include <base/Timestamp.h>
#include <log/Logger.h>

#include <cstdlib>

bool AccessLogMiddleware::before(HttpRequest& request, HttpResponse&)
{
    request.setAttribute("access_log_start_us",
                         std::to_string(Timestamp::now().microSecondsSinceEpoch()));
    return true;
}

void AccessLogMiddleware::after(const HttpRequest& request, HttpResponse& response)
{
    const std::string start = request.attribute("access_log_start_us");
    const long long startUs = start.empty() ? 0 : std::strtoll(start.c_str(), 0, 10);
    const long long nowUs = Timestamp::now().microSecondsSinceEpoch();
    const long long latencyUs = startUs > 0 && nowUs >= startUs ? nowUs - startUs : 0;
    LOG_INFO << "request_id=" << request.attribute("request_id")
             << " method=" << HttpRequest::methodString(request.method())
             << " target=" << sanitizeTarget(request.path())
             << " status=" << response.statusCode()
             << " latency_us=" << latencyUs;
}

std::string AccessLogMiddleware::sanitizeTarget(const std::string& target)
{
    const std::string::size_type query = target.find('?');
    return query == std::string::npos ? target : target.substr(0, query);
}
