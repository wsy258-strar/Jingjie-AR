#include <handlers/PresenceHandlers.h>

#include <base/TaskWorkerPool.h>
#include <base/Timestamp.h>
#include <services/PresenceService.h>
#include <services/SessionService.h>
#include <utils/ApiError.h>
#include <utils/JsonUtil.h>

#include <sstream>

namespace ar {
namespace {

int64_t nowMs()
{
    return Timestamp::now().microSecondsSinceEpoch() / 1000;
}

struct MemberRecord
{
    MemberRecord(uint64_t member, uint64_t user, int64_t seen)
        : memberId(member), userId(user), lastSeenMs(seen) {}
    uint64_t memberId;
    uint64_t userId;
    int64_t lastSeenMs;
};

struct MembersState
{
    MembersState(SessionService* sessionService, const std::string& requestedScene,
                 const AsyncResponder& reply, const std::string& id)
        : sessions(sessionService), scene(requestedScene), responder(reply), requestId(id), next(0) {}
    SessionService* sessions;
    std::string scene;
    AsyncResponder responder;
    std::string requestId;
    std::vector<PresenceEntry> entries;
    std::vector<MemberRecord> members;
    size_t next;
};

void resolveMember(const std::shared_ptr<MembersState>& state)
{
    if (state->next >= state->entries.size())
    {
        std::ostringstream json;
        json << "{\"members\":[";
        for (size_t index = 0; index < state->members.size(); ++index)
        {
            if (index) json << ',';
            const MemberRecord& member = state->members[index];
            json << "{\"member_id\":" << member.memberId
                 << ",\"user_id\":" << member.userId
                 << ",\"last_seen_ms\":" << member.lastSeenMs << '}';
        }
        json << "]}";
        HttpResponse response(false);
        response.setContentType("application/json; charset=utf-8");
        response.setBody(json.str());
        state->responder.send(response);
        return;
    }
    const PresenceEntry entry = state->entries[state->next++];
    state->sessions->get(entry.token, [state, entry](const std::shared_ptr<Session>& session) {
        if (session && session->status == 1 && session->sceneId == state->scene)
            state->members.push_back(MemberRecord(session->id, session->userId, entry.lastSeenMs));
        resolveMember(state);
    });
}

} // namespace

void PresenceHandlers::heartbeat(const HttpRequest& request, const AsyncResponder& responder) const
{
    const std::map<std::string, std::string>& params = request.queryParameters();
    std::map<std::string, std::string>::const_iterator token = params.find("token");
    std::map<std::string, std::string>::const_iterator scene = params.find("scene");
    const std::string tokenValue = token == params.end() ? request.attribute("auth.token") : token->second;
    if (tokenValue.empty() || scene == params.end() || scene->second.empty())
    {
        responder.send(makeApiError(HttpResponse::k400BadRequest, "BAD_REQUEST",
                                    "missing token or scene", request.attribute("request_id")));
        return;
    }
    if (!presence_ || !sessions_ || !cacheWorkers_)
    {
        responder.send(makeApiError(HttpResponse::k503ServiceUnavailable, "SERVICE_UNAVAILABLE",
                                    "presence service unavailable", request.attribute("request_id")));
        return;
    }
    const std::string requestedScene = scene->second;
    const std::string requestId = request.attribute("request_id");
    sessions_->get(tokenValue, [this, requestedScene, tokenValue, responder, requestId]
                   (const std::shared_ptr<Session>& session) {
        if (!session || session->status != 1 || session->sceneId != requestedScene)
        {
            responder.send(makeApiError(HttpResponse::k403Forbidden, "FORBIDDEN",
                                        "token is not active in this scene", requestId));
            return;
        }
        if (!cacheWorkers_->submit([this, requestedScene, tokenValue, responder, requestId]() {
            if (presence_->heartbeat(requestedScene, tokenValue, nowMs()))
            {
                HttpResponse response(false);
                response.setContentType("application/json; charset=utf-8");
                response.setBody("{\"status\":\"ok\"}");
                responder.send(response);
            }
            else
            {
                responder.send(makeApiError(HttpResponse::k503ServiceUnavailable, "SERVICE_UNAVAILABLE",
                                            "presence service unavailable", requestId));
            }
        }))
        {
            responder.send(makeApiError(HttpResponse::k503ServiceUnavailable, "SERVICE_UNAVAILABLE",
                                        "presence worker queue is full", requestId));
        }
    });
}

void PresenceHandlers::members(const HttpRequest& request, const AsyncResponder& responder) const
{
    const std::string scene = request.pathParameter("sceneId");
    if (scene.empty())
    {
        responder.send(makeApiError(HttpResponse::k400BadRequest, "BAD_REQUEST", "missing scene id",
                                    request.attribute("request_id")));
        return;
    }
    if (!presence_ || !sessions_ || !cacheWorkers_)
    {
        responder.send(makeApiError(HttpResponse::k503ServiceUnavailable, "SERVICE_UNAVAILABLE",
                                    "presence service unavailable", request.attribute("request_id")));
        return;
    }
    const std::string requestId = request.attribute("request_id");
    if (!cacheWorkers_->submit([this, scene, responder, requestId]() {
        std::shared_ptr<MembersState> state(new MembersState(sessions_, scene, responder, requestId));
        if (!presence_->list(scene, nowMs(), &state->entries))
        {
            responder.send(makeApiError(HttpResponse::k503ServiceUnavailable, "SERVICE_UNAVAILABLE",
                                        "presence service unavailable", requestId));
            return;
        }
        resolveMember(state);
    }))
    {
        responder.send(makeApiError(HttpResponse::k503ServiceUnavailable, "SERVICE_UNAVAILABLE",
                                    "presence worker queue is full", requestId));
    }
}

} // namespace ar
