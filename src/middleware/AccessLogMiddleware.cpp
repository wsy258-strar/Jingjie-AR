#include <middleware/AccessLogMiddleware.h>

#include <algorithm>
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
    std::string target = request.path();
    if (!request.query().empty()) target += "?" + request.query();
    LOG_INFO << "request_id=" << request.attribute("request_id")
             << " method=" << HttpRequest::methodString(request.method())
             << " target=" << sanitizeTarget(target)
             << " status=" << response.statusCode()
             << " latency_us=" << latencyUs;
}

std::string AccessLogMiddleware::sanitizeTarget(const std::string& target)
{
    const std::string key("password=");
    std::string lower(target);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    const std::string::size_type position = lower.find(key);
    if (position == std::string::npos) return target;
    const std::string::size_type valueStart = position + key.size();
    const std::string::size_type valueEnd = target.find('&', valueStart);
    return target.substr(0, valueStart) + "%5BREDACTED%5D" +
           (valueEnd == std::string::npos ? std::string() : target.substr(valueEnd));
}
