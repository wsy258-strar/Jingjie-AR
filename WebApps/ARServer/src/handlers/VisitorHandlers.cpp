// 匿名访客 HTTP 处理实现：Redis 工作移出 EventLoop，MySQL 失败按统计不可用降级。
#include <handlers/VisitorHandlers.h>

#include <base/TaskWorkerPool.h>
#include <base/Timestamp.h>
#include <db/ExhibitionStatisticsDAO.h>
#include <services/PresenceService.h>
#include <services/VisitorSessionService.h>
#include <utils/ApiError.h>
#include <utils/ApiResponse.h>
#include <utils/JsonUtil.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ar {
namespace {

int64_t nowMs()
{
    return Timestamp::now().microSecondsSinceEpoch() / 1000;
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

bool isJsonContentType(const HttpRequest& request)
{
    std::string contentType = lower(request.getHeader("content-type"));
    const std::string::size_type separator = contentType.find(';');
    if (separator != std::string::npos) contentType.erase(separator);
    while (!contentType.empty() &&
           std::isspace(static_cast<unsigned char>(contentType[contentType.size() - 1])))
        contentType.erase(contentType.size() - 1);
    std::string::size_type begin = 0;
    while (begin < contentType.size() &&
           std::isspace(static_cast<unsigned char>(contentType[begin])))
        ++begin;
    return contentType.substr(begin) == "application/json";
}

bool bootstrapRequestId(const std::string& body, std::string* value)
{
    if (!value) return false;
    const nlohmann::json json = nlohmann::json::parse(body, 0, false);
    if (json.is_discarded() || !json.is_object()) return false;
    const nlohmann::json::const_iterator found = json.find("bootstrapRequestId");
    if (found == json.end() || !found->is_string()) return false;
    *value = found->get<std::string>();
    return true;
}

HttpResponse unavailable(const std::string& message, const std::string& requestId)
{
    return makeApiError(HttpResponse::k503ServiceUnavailable, "SERVICE_UNAVAILABLE",
                        message, requestId);
}

HttpResponse visitorRequired(const std::string& requestId)
{
    return makeApiError(HttpResponse::k401Unauthorized, "VISITOR_TOKEN_REQUIRED",
                        "visitor token is required", requestId);
}

HttpResponse visitorInvalid(const std::string& requestId)
{
    return makeApiError(HttpResponse::k401Unauthorized, "VISITOR_TOKEN_INVALID",
                        "visitor token is invalid or expired", requestId);
}

bool visitorTokenSyntax(const std::string& token)
{
    if (token.size() != 64) return false;
    for (size_t index = 0; index < token.size(); ++index)
    {
        const char value = token[index];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')))
            return false;
    }
    return true;
}

std::string bootstrapJson(const std::string& token, bool statisticsAvailable, uint64_t count)
{
    std::ostringstream output;
    output << "{\"visitorToken\":\"" << JsonUtil::escape(token) << "\",\"totalViews\":";
    if (statisticsAvailable)
        output << count;
    else
        output << "null";
    output << ",\"statisticsAvailable\":"
           << (statisticsAvailable ? "true" : "false") << '}';
    return output.str();
}

std::string statisticsJson(bool statisticsAvailable, uint64_t count)
{
    std::ostringstream output;
    output << "{\"totalViews\":";
    if (statisticsAvailable)
        output << count;
    else
        output << "null";
    output << ",\"statisticsAvailable\":"
           << (statisticsAvailable ? "true" : "false") << '}';
    return output.str();
}

} // namespace

void VisitorHandlers::bootstrap(const HttpRequest& request,
                                const AsyncResponder& responder) const
{
    const std::string requestId = request.attribute("request_id");
    if (!isJsonContentType(request))
    {
        responder.send(makeApiError(HttpResponse::k400BadRequest, "INVALID_CONTENT_TYPE",
                                    "content type must be application/json", requestId));
        return;
    }

    std::string bootstrapId;
    if (!bootstrapRequestId(request.body(), &bootstrapId))
    {
        responder.send(makeApiError(HttpResponse::k400BadRequest, "INVALID_JSON",
                                    "bootstrapRequestId must be a JSON string", requestId));
        return;
    }
    if (!visitors_ || !presence_ || !statistics_ || !cacheWorkers_ || exhibitionId_.empty())
    {
        responder.send(unavailable("visitor service unavailable", requestId));
        return;
    }

    VisitorSessionService* const visitors = visitors_;
    PresenceService* const presence = presence_;
    ExhibitionStatisticsDAO* const statistics = statistics_;
    const std::string exhibitionId = exhibitionId_;
    const std::string existingToken = request.getHeader("X-Visitor-Token");
    if (!cacheWorkers_->submit(
        [visitors, presence, statistics, exhibitionId, existingToken, bootstrapId,
         responder, requestId]() {
            try
            {
                const VisitorBootstrapResult result =
                    visitors->bootstrap(existingToken, bootstrapId);
                if (result.status == VisitorBootstrapResult::kBadRequest)
                {
                    responder.send(makeApiError(
                        HttpResponse::k400BadRequest, "INVALID_BOOTSTRAP_REQUEST_ID",
                        "bootstrapRequestId has invalid syntax", requestId));
                    return;
                }
                if (result.status != VisitorBootstrapResult::kOk)
                {
                    responder.send(unavailable("visitor identity service unavailable", requestId));
                    return;
                }
                if (!presence->heartbeat(result.token, nowMs()))
                {
                    responder.send(unavailable("presence service unavailable", requestId));
                    return;
                }
                const std::string token = result.token;
                statistics->incrementAndRead(
                    exhibitionId, bootstrapId,
                    [token, responder](bool ok, uint64_t count) {
                        responder.send(makeApiSuccess(bootstrapJson(token, ok, count)));
                    });
            }
            catch (...)
            {
                responder.send(unavailable("visitor service unavailable", requestId));
            }
        }))
    {
        responder.send(unavailable("visitor worker queue is full", requestId));
    }
}

void VisitorHandlers::heartbeat(const HttpRequest& request,
                                const AsyncResponder& responder) const
{
    const std::string token = request.getHeader("X-Visitor-Token");
    const std::string requestId = request.attribute("request_id");
    if (token.empty())
    {
        responder.send(visitorRequired(requestId));
        return;
    }
    if (!visitorTokenSyntax(token))
    {
        responder.send(visitorInvalid(requestId));
        return;
    }
    if (!visitors_ || !presence_ || !cacheWorkers_)
    {
        responder.send(unavailable("presence service unavailable", requestId));
        return;
    }

    VisitorSessionService* const visitors = visitors_;
    PresenceService* const presence = presence_;
    if (!cacheWorkers_->submit([visitors, presence, token, responder, requestId]() {
        try
        {
            if (!visitors->refresh(token))
            {
                responder.send(unavailable("visitor identity service unavailable", requestId));
                return;
            }
            if (!presence->heartbeat(token, nowMs()))
            {
                responder.send(unavailable("presence service unavailable", requestId));
                return;
            }
            responder.send(makeApiSuccess("{}"));
        }
        catch (...)
        {
            responder.send(unavailable("presence service unavailable", requestId));
        }
    }))
    {
        responder.send(unavailable("presence worker queue is full", requestId));
    }
}

void VisitorHandlers::exit(const HttpRequest& request,
                           const AsyncResponder& responder) const
{
    const std::string token = request.getHeader("X-Visitor-Token");
    const std::string requestId = request.attribute("request_id");
    if (token.empty())
    {
        responder.send(visitorRequired(requestId));
        return;
    }
    if (!visitorTokenSyntax(token))
    {
        responder.send(visitorInvalid(requestId));
        return;
    }
    if (!visitors_ || !presence_ || !cacheWorkers_)
    {
        responder.send(unavailable("presence service unavailable", requestId));
        return;
    }

    VisitorSessionService* const visitors = visitors_;
    PresenceService* const presence = presence_;
    if (!cacheWorkers_->submit([visitors, presence, token, responder, requestId]() {
        try
        {
            if (!visitors->valid(token))
            {
                responder.send(unavailable("visitor identity service unavailable", requestId));
                return;
            }
            if (!presence->remove(token))
            {
                responder.send(unavailable("presence service unavailable", requestId));
                return;
            }
            responder.send(makeApiSuccess("{}"));
        }
        catch (...)
        {
            responder.send(unavailable("presence service unavailable", requestId));
        }
    }))
    {
        responder.send(unavailable("presence worker queue is full", requestId));
    }
}

void VisitorHandlers::presence(const HttpRequest& request,
                               const AsyncResponder& responder) const
{
    const std::string requestId = request.attribute("request_id");
    if (!presence_ || !cacheWorkers_)
    {
        responder.send(unavailable("presence service unavailable", requestId));
        return;
    }

    PresenceService* const presence = presence_;
    if (!cacheWorkers_->submit([presence, responder, requestId]() {
        try
        {
            uint64_t count = 0;
            if (!presence->count(nowMs(), &count))
            {
                responder.send(unavailable("presence service unavailable", requestId));
                return;
            }
            std::ostringstream output;
            output << "{\"onlineCount\":" << count << '}';
            responder.send(makeApiSuccess(output.str()));
        }
        catch (...)
        {
            responder.send(unavailable("presence service unavailable", requestId));
        }
    }))
    {
        responder.send(unavailable("presence worker queue is full", requestId));
    }
}

void VisitorHandlers::views(const HttpRequest& request,
                            const AsyncResponder& responder) const
{
    const std::string requestId = request.attribute("request_id");
    if (!statistics_ || exhibitionId_.empty())
    {
        responder.send(makeApiSuccess(statisticsJson(false, 0)));
        return;
    }

    statistics_->read(exhibitionId_, [responder](bool ok, uint64_t count) {
        responder.send(makeApiSuccess(statisticsJson(ok, count)));
    });
}

} // namespace ar
