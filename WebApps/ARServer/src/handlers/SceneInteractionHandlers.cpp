#include <handlers/SceneInteractionHandlers.h>

#include <catalog/SceneCatalog.h>
#include <services/SceneInteractionService.h>
#include <utils/ApiError.h>
#include <utils/JsonUtil.h>

#include <cstdlib>
#include <sstream>

namespace ar {
namespace {

HttpResponse failure(const SceneInteractionResult& result, const std::string& requestId)
{
    switch (result.status)
    {
    case SceneInteractionResult::kUnauthorized:
        return makeApiError(HttpResponse::k401Unauthorized, "UNAUTHORIZED", "authentication token is required", requestId);
    case SceneInteractionResult::kBadRequest:
        return makeApiError(HttpResponse::k400BadRequest, "BAD_REQUEST", "invalid comment content", requestId);
    case SceneInteractionResult::kNotFound:
        return makeApiError(HttpResponse::k404NotFound, "SCENE_NOT_FOUND", "scene not found", requestId);
    case SceneInteractionResult::kUnavailable:
        return makeApiError(HttpResponse::k503ServiceUnavailable, "SERVICE_UNAVAILABLE", "scene interaction service unavailable", requestId);
    case SceneInteractionResult::kOk:
        break;
    }
    return HttpResponse(false);
}

void sendResult(const SceneInteractionResult& result, const std::string& requestId,
                const AsyncResponder& responder, const std::string& body)
{
    if (result.status != SceneInteractionResult::kOk)
    {
        responder.send(failure(result, requestId));
        return;
    }
    HttpResponse response(false);
    response.setContentType("application/json; charset=utf-8");
    response.setBody(body);
    responder.send(response);
}

bool jsonStringField(const std::string& body, const std::string& field, std::string* value)
{
    const std::string key = "\"" + field + "\"";
    const std::string::size_type found = body.find(key);
    if (found == std::string::npos) return false;
    std::string::size_type cursor = body.find(':', found + key.size());
    if (cursor == std::string::npos) return false;
    while (++cursor < body.size() && (body[cursor] == ' ' || body[cursor] == '\t' || body[cursor] == '\n' || body[cursor] == '\r')) {}
    if (cursor >= body.size() || body[cursor] != '"') return false;
    value->clear();
    for (++cursor; cursor < body.size(); ++cursor)
    {
        const char character = body[cursor];
        if (character == '"') return true;
        if (character != '\\') { value->push_back(character); continue; }
        if (++cursor >= body.size()) return false;
        switch (body[cursor])
        {
        case '"': value->push_back('"'); break;
        case '\\': value->push_back('\\'); break;
        case '/': value->push_back('/'); break;
        case 'b': value->push_back('\b'); break;
        case 'f': value->push_back('\f'); break;
        case 'n': value->push_back('\n'); break;
        case 'r': value->push_back('\r'); break;
        case 't': value->push_back('\t'); break;
        default: return false;
        }
    }
    return false;
}

uint64_t numberParameter(const HttpRequest& request, const std::string& name)
{
    const std::map<std::string, std::string>& parameters = request.queryParameters();
    const std::map<std::string, std::string>::const_iterator it = parameters.find(name);
    if (it == parameters.end() || it->second.empty()) return 0;
    char* end = 0;
    const unsigned long long value = std::strtoull(it->second.c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<uint64_t>(value) : 0;
}

std::string sceneJson(const SceneInfo& scene, uint64_t likes)
{
    std::ostringstream output;
    output << "{\"id\":\"" << JsonUtil::escape(scene.id) << "\",\"name\":\""
           << JsonUtil::escape(scene.name) << "\",\"panorama_url\":\""
           << JsonUtil::escape(scene.panoramaUrl) << "\",\"thumbnail_url\":\""
           << JsonUtil::escape(scene.thumbnailUrl)
           << "\",\"music_url\":";
    if (scene.musicUrl.empty()) output << "null";
    else output << "\"" << JsonUtil::escape(scene.musicUrl) << "\"";
    output << ",\"like_count\":" << likes << "}";
    return output.str();
}

} // namespace

void SceneInteractionHandlers::detail(const HttpRequest& request, const AsyncResponder& responder) const
{
    const std::string sceneId = request.pathParameter("sceneId");
    const SceneInfo* scene = SceneCatalog::find(sceneId);
    if (!service_ || !scene)
    {
        SceneInteractionResult result;
        result.status = scene ? SceneInteractionResult::kUnavailable : SceneInteractionResult::kNotFound;
        responder.send(failure(result, request.attribute("request_id")));
        return;
    }
    const std::string requestId = request.attribute("request_id");
    service_->detail(sceneId, [scene, requestId, responder](const SceneInteractionResult& result) {
        if (result.status != SceneInteractionResult::kOk) { responder.send(failure(result, requestId)); return; }
        HttpResponse response(false);
        response.setContentType("application/json; charset=utf-8");
        response.setBody(sceneJson(*scene, result.likeCount));
        responder.send(response);
    });
}

void SceneInteractionHandlers::like(const HttpRequest& request, const AsyncResponder& responder) const
{
    if (!service_)
    {
        SceneInteractionResult result; result.status = SceneInteractionResult::kUnavailable;
        responder.send(failure(result, request.attribute("request_id"))); return;
    }
    const std::string requestId = request.attribute("request_id");
    service_->like(request.attribute("auth.token"), request.pathParameter("sceneId"),
                   [requestId, responder](const SceneInteractionResult& result) {
        std::ostringstream output; output << "{\"status\":\"ok\",\"liked\":"
                                         << (result.liked ? "true" : "false")
                                         << ",\"like_count\":" << result.likeCount << "}";
        sendResult(result, requestId, responder, output.str());
    });
}

void SceneInteractionHandlers::unlike(const HttpRequest& request, const AsyncResponder& responder) const
{
    if (!service_)
    {
        SceneInteractionResult result; result.status = SceneInteractionResult::kUnavailable;
        responder.send(failure(result, request.attribute("request_id"))); return;
    }
    const std::string requestId = request.attribute("request_id");
    service_->unlike(request.attribute("auth.token"), request.pathParameter("sceneId"),
                     [requestId, responder](const SceneInteractionResult& result) {
        std::ostringstream output; output << "{\"status\":\"ok\",\"liked\":"
                                         << (result.liked ? "true" : "false")
                                         << ",\"like_count\":" << result.likeCount << "}";
        sendResult(result, requestId, responder, output.str());
    });
}

void SceneInteractionHandlers::comments(const HttpRequest& request, const AsyncResponder& responder) const
{
    if (!service_)
    {
        SceneInteractionResult result; result.status = SceneInteractionResult::kUnavailable;
        responder.send(failure(result, request.attribute("request_id"))); return;
    }
    const std::string requestId = request.attribute("request_id");
    service_->listComments(request.pathParameter("sceneId"), numberParameter(request, "before"),
                           static_cast<uint32_t>(numberParameter(request, "limit")),
                           [requestId, responder](const SceneInteractionResult& result) {
        if (result.status != SceneInteractionResult::kOk) { responder.send(failure(result, requestId)); return; }
        std::ostringstream output; output << "{\"comments\":[";
        for (size_t index = 0; index < result.comments.size(); ++index)
        {
            if (index) output << ',';
            output << "{\"id\":" << result.comments[index].id << ",\"username\":\""
                   << JsonUtil::escape(result.comments[index].username) << "\",\"content\":\""
                   << JsonUtil::escape(result.comments[index].content) << "\"}";
        }
        output << "],\"next_before\":" << result.nextBefore << "}";
        HttpResponse response(false); response.setContentType("application/json; charset=utf-8");
        response.setBody(output.str()); responder.send(response);
    });
}

void SceneInteractionHandlers::comment(const HttpRequest& request, const AsyncResponder& responder) const
{
    std::string content;
    if (!jsonStringField(request.body(), "content", &content))
    {
        responder.send(makeApiError(HttpResponse::k400BadRequest, "BAD_REQUEST", "invalid comment content",
                                    request.attribute("request_id")));
        return;
    }
    if (!service_)
    {
        SceneInteractionResult result; result.status = SceneInteractionResult::kUnavailable;
        responder.send(failure(result, request.attribute("request_id"))); return;
    }
    const std::string requestId = request.attribute("request_id");
    service_->comment(request.attribute("auth.token"), request.pathParameter("sceneId"), content,
                      [requestId, responder](const SceneInteractionResult& result) {
        std::ostringstream output; output << "{\"status\":\"ok\",\"comment_id\":" << result.nextBefore << "}";
        sendResult(result, requestId, responder, output.str());
    });
}

} // namespace ar
