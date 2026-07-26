#include <handlers/SessionHandlers.h>

#include <utils/JsonUtil.h>
#include <utils/ApiError.h>
#include <services/SessionService.h>
#include <services/PresenceService.h>
#include <base/TaskWorkerPool.h>
#include <base/Timestamp.h>

#include <sstream>
#include <chrono>
#include <thread>

namespace ar {
namespace {

int64_t presenceNowMs()
{
    return Timestamp::now().microSecondsSinceEpoch() / 1000;
}

}

std::string SessionHandlers::json(const Session& session)
{
    std::ostringstream output;
    output << "{\"status\":\"ok\",\"session_id\":" << session.id
           << ",\"token\":\"" << JsonUtil::escape(session.sessionToken)
           << "\",\"user_id\":" << session.userId
           << ",\"scene_id\":\"" << JsonUtil::escape(session.sceneId)
           << "\",\"status_code\":" << session.status
           << ",\"created_at\":\"" << JsonUtil::escape(session.createdAt)
           << "\",\"updated_at\":\"" << JsonUtil::escape(session.updatedAt)
           << "\"}";
    return output.str();
}

void SessionHandlers::get(const HttpRequest& request, const AsyncResponder& responder) const
{
    const std::map<std::string, std::string>& parameters = request.queryParameters();
    const std::map<std::string, std::string>::const_iterator token = parameters.find("token");
    if (!service_ || token == parameters.end() || token->second.empty())
    {
        HttpResponse response(false);
        response.setStatusCode(service_ ? HttpResponse::k400BadRequest : HttpResponse::k503ServiceUnavailable);
        response.setContentType("application/json; charset=utf-8");
        response.setBody(service_ ? "{\"error\":\"missing token\"}" : "{\"error\":\"service unavailable\"}");
        responder.send(response);
        return;
    }
    const std::string tokenValue = token->second;
    const auto complete = [responder](const std::shared_ptr<Session>& session) {
        HttpResponse response(false);
        response.setContentType("application/json; charset=utf-8");
        if (session) response.setBody(json(*session));
        else response.setBody("{\"status\":\"not_found\"}");
        responder.send(response);
    };
    if (testDbDelayMs_ > 0 && cacheWorkers_)
    {
        const int delayMs = testDbDelayMs_;
        if (cacheWorkers_->submit([this, tokenValue, complete, delayMs]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            service_->get(tokenValue, complete);
        })) return;
        responder.send(makeApiError(HttpResponse::k503ServiceUnavailable, "SERVICE_UNAVAILABLE",
                                    "test delay worker queue is full", request.attribute("request_id")));
        return;
    }
    service_->get(tokenValue, complete);
}

void SessionHandlers::enter(const HttpRequest& request, const AsyncResponder& responder) const
{
    const std::map<std::string, std::string>& parameters = request.queryParameters();
    const std::map<std::string, std::string>::const_iterator token = parameters.find("token");
    const std::map<std::string, std::string>::const_iterator scene = parameters.find("scene");
    if (!service_ || token == parameters.end() || scene == parameters.end() || token->second.empty() || scene->second.empty())
    {
        HttpResponse response(false); response.setStatusCode(service_ ? HttpResponse::k400BadRequest : HttpResponse::k503ServiceUnavailable);
        response.setContentType("application/json; charset=utf-8"); response.setBody("{\"error\":\"missing token or scene\"}"); responder.send(response); return;
    }
    const std::string sceneId = scene->second;
    const std::string tokenValue = token->second;
    service_->enter(tokenValue, sceneId, [this, responder, sceneId, tokenValue](bool ok) {
        HttpResponse response(false); response.setContentType("application/json; charset=utf-8");
        if (!ok) { response.setStatusCode(HttpResponse::k503ServiceUnavailable); response.setBody("{\"error\":\"enter scene failed\"}"); responder.send(response); return; }
        if (!presence_ || !cacheWorkers_)
        {
            response.setBody("{\"status\":\"ok\",\"scene_id\":\"" + JsonUtil::escape(sceneId) + "\",\"presence_available\":false}");
            responder.send(response);
            return;
        }
        if (!cacheWorkers_->submit([this, responder, sceneId, tokenValue]() {
            HttpResponse asyncResponse(false);
            asyncResponse.setContentType("application/json; charset=utf-8");
            asyncResponse.setBody("{\"status\":\"ok\",\"scene_id\":\"" + JsonUtil::escape(sceneId) + "\",\"presence_available\":" +
                                  (presence_->heartbeat(sceneId, tokenValue, presenceNowMs()) ? "true" : "false") + "}");
            responder.send(asyncResponse);
        }))
        {
            response.setBody("{\"status\":\"ok\",\"scene_id\":\"" + JsonUtil::escape(sceneId) + "\",\"presence_available\":false}");
            responder.send(response);
        }
    });
}

void SessionHandlers::exit(const HttpRequest& request, const AsyncResponder& responder) const
{
    const std::map<std::string, std::string>& parameters = request.queryParameters();
    const std::map<std::string, std::string>::const_iterator token = parameters.find("token");
    if (!service_ || token == parameters.end() || token->second.empty())
    {
        HttpResponse response(false); response.setStatusCode(service_ ? HttpResponse::k400BadRequest : HttpResponse::k503ServiceUnavailable);
        response.setContentType("application/json; charset=utf-8"); response.setBody("{\"error\":\"missing token\"}"); responder.send(response); return;
    }
    const std::string tokenValue = token->second;
    const std::string requestId = request.attribute("request_id");
    service_->get(tokenValue, [this, tokenValue, responder, requestId](const std::shared_ptr<Session>& session) {
        if (!session || session->status == 0)
        {
            responder.send(makeApiError(HttpResponse::k409Conflict, "SESSION_CONFLICT", "already exited", requestId));
            return;
        }
        const std::string sceneId = session->sceneId;
        service_->exit(tokenValue, [this, tokenValue, sceneId, responder](bool ok) {
        HttpResponse response(false); response.setContentType("application/json; charset=utf-8");
        if (ok) response.setBody("{\"status\":\"ok\"}");
        else { response.setStatusCode(HttpResponse::k409Conflict); response.setBody("{\"error\":\"already exited\"}"); }
        responder.send(response);
        if (ok && presence_ && cacheWorkers_ && !sceneId.empty())
            cacheWorkers_->submit([this, sceneId, tokenValue]() { presence_->remove(sceneId, tokenValue); });
        });
    });
}

} // namespace ar
