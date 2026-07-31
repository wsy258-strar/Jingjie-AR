// 作品互动 HTTP 处理实现：所有成功与失败路径均输出统一 API 信封。
#include <handlers/ArtworkInteractionHandlers.h>

#include <catalog/ExhibitionCatalog.h>
#include <services/ArtworkInteractionService.h>
#include <utils/ApiError.h>
#include <utils/ApiResponse.h>
#include <utils/JsonUtil.h>

#include <nlohmann/json.hpp>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <sstream>

namespace ar {
namespace {

HttpResponse failure(const ArtworkInteractionResult& result, const std::string& requestId)
{
    switch (result.status)
    {
    case ArtworkInteractionResult::kUnauthorized:
        return makeApiError(HttpResponse::k401Unauthorized, "UNAUTHORIZED",
                            "authentication token is required", requestId);
    case ArtworkInteractionResult::kBadRequest:
        return makeApiError(HttpResponse::k400BadRequest, "INVALID_COMMENT",
                            "comment content must contain 1 to 1000 bytes", requestId);
    case ArtworkInteractionResult::kNotFound:
        return makeApiError(HttpResponse::k404NotFound, "ARTWORK_NOT_FOUND",
                            "artwork not found", requestId);
    case ArtworkInteractionResult::kUnavailable:
        return makeApiError(HttpResponse::k503ServiceUnavailable, "SERVICE_UNAVAILABLE",
                            "artwork interaction service unavailable", requestId);
    case ArtworkInteractionResult::kOk:
        break;
    }
    return makeApiError(HttpResponse::k500InternalServerError, "INTERNAL_ERROR",
                        "unexpected artwork interaction result", requestId);
}

void writeString(std::ostringstream* output, const std::string& value)
{
    *output << '"' << JsonUtil::escape(value) << '"';
}

std::string summaryJson(bool liked, uint64_t likeCount)
{
    std::ostringstream output;
    output << "{\"liked\":" << (liked ? "true" : "false")
           << ",\"likeCount\":" << likeCount << '}';
    return output.str();
}

std::string artworkJson(const ArtworkInfo& artwork, const ArtworkInteractionResult& result)
{
    std::ostringstream output;
    output << "{\"artworkId\":";
    writeString(&output, artwork.id);
    output << ",\"title\":";
    writeString(&output, artwork.title);
    output << ",\"text\":";
    writeString(&output, artwork.text);
    output << ",\"images\":[";
    for (size_t index = 0; index < artwork.images.size(); ++index)
    {
        if (index) output << ',';
        writeString(&output, artwork.images[index]);
    }
    output << "],\"likeCount\":" << result.likeCount
           << ",\"liked\":" << (result.liked ? "true" : "false") << '}';
    return output.str();
}

std::string commentsJson(const ArtworkInteractionResult& result)
{
    std::ostringstream output;
    output << "{\"comments\":[";
    for (size_t index = 0; index < result.comments.size(); ++index)
    {
        if (index) output << ',';
        output << "{\"id\":" << result.comments[index].id << ",\"username\":";
        writeString(&output, result.comments[index].username);
        output << ",\"content\":";
        writeString(&output, result.comments[index].content);
        output << '}';
    }
    output << "],\"nextBefore\":" << result.nextBefore << '}';
    return output.str();
}

bool unsignedParameter(const HttpRequest& request, const std::string& name, uint64_t* value)
{
    const std::map<std::string, std::string>& parameters = request.queryParameters();
    const std::map<std::string, std::string>::const_iterator found = parameters.find(name);
    if (found == parameters.end() || found->second.empty())
    {
        *value = 0;
        return true;
    }
    errno = 0;
    char* end = 0;
    const unsigned long long parsed = std::strtoull(found->second.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end != '\0' || found->second[0] == '-')
        return false;
    *value = static_cast<uint64_t>(parsed);
    return true;
}

bool commentContent(const std::string& body, std::string* content)
{
    const nlohmann::json value = nlohmann::json::parse(body, 0, false);
    if (value.is_discarded() || !value.is_object()) return false;
    const nlohmann::json::const_iterator found = value.find("content");
    if (found == value.end() || !found->is_string()) return false;
    *content = found->get<std::string>();
    return true;
}

void unavailable(const HttpRequest& request, const AsyncResponder& responder)
{
    ArtworkInteractionResult result;
    result.status = ArtworkInteractionResult::kUnavailable;
    responder.send(failure(result, request.attribute("request_id")));
}

} // namespace

void ArtworkInteractionHandlers::detail(const HttpRequest& request,
                                        const AsyncResponder& responder) const
{
    if (!service_)
    {
        unavailable(request, responder);
        return;
    }
    const std::string artworkId = request.pathParameter("artworkId");
    const ArtworkInfo* artwork = service_->findArtwork(artworkId);
    const std::string requestId = request.attribute("request_id");
    service_->detail(
        request.attribute("auth.token"), artworkId,
        [artwork, requestId, responder](const ArtworkInteractionResult& result) {
            if (result.status != ArtworkInteractionResult::kOk)
            {
                responder.send(failure(result, requestId));
                return;
            }
            if (!artwork)
            {
                ArtworkInteractionResult missing;
                missing.status = ArtworkInteractionResult::kNotFound;
                responder.send(failure(missing, requestId));
                return;
            }
            responder.send(makeApiSuccess(artworkJson(*artwork, result)));
        });
}

void ArtworkInteractionHandlers::like(const HttpRequest& request,
                                      const AsyncResponder& responder) const
{
    if (!service_)
    {
        unavailable(request, responder);
        return;
    }
    const std::string requestId = request.attribute("request_id");
    service_->like(
        request.attribute("auth.token"), request.pathParameter("artworkId"),
        [requestId, responder](const ArtworkInteractionResult& result) {
            if (result.status != ArtworkInteractionResult::kOk)
                responder.send(failure(result, requestId));
            else
                responder.send(makeApiSuccess(summaryJson(result.liked, result.likeCount)));
        });
}

void ArtworkInteractionHandlers::unlike(const HttpRequest& request,
                                        const AsyncResponder& responder) const
{
    if (!service_)
    {
        unavailable(request, responder);
        return;
    }
    const std::string requestId = request.attribute("request_id");
    service_->unlike(
        request.attribute("auth.token"), request.pathParameter("artworkId"),
        [requestId, responder](const ArtworkInteractionResult& result) {
            if (result.status != ArtworkInteractionResult::kOk)
                responder.send(failure(result, requestId));
            else
                responder.send(makeApiSuccess(summaryJson(result.liked, result.likeCount)));
        });
}

void ArtworkInteractionHandlers::comments(const HttpRequest& request,
                                          const AsyncResponder& responder) const
{
    if (!service_)
    {
        unavailable(request, responder);
        return;
    }
    uint64_t before = 0;
    uint64_t limit = 0;
    if (!unsignedParameter(request, "before", &before) ||
        !unsignedParameter(request, "limit", &limit))
    {
        responder.send(makeApiError(HttpResponse::k400BadRequest, "INVALID_PAGINATION",
                                    "before and limit must be unsigned integers",
                                    request.attribute("request_id")));
        return;
    }
    const uint32_t boundedLimit =
        limit > static_cast<uint64_t>(UINT_MAX) ? UINT_MAX : static_cast<uint32_t>(limit);
    const std::string requestId = request.attribute("request_id");
    service_->listComments(
        request.pathParameter("artworkId"), before, boundedLimit,
        [requestId, responder](const ArtworkInteractionResult& result) {
            if (result.status != ArtworkInteractionResult::kOk)
                responder.send(failure(result, requestId));
            else
                responder.send(makeApiSuccess(commentsJson(result)));
        });
}

void ArtworkInteractionHandlers::comment(const HttpRequest& request,
                                         const AsyncResponder& responder) const
{
    std::string content;
    const bool contentValid = commentContent(request.body(), &content);
    if (!service_)
    {
        unavailable(request, responder);
        return;
    }
    const std::string requestId = request.attribute("request_id");
    service_->commentRequest(
        request.attribute("auth.token"), request.pathParameter("artworkId"), content,
        contentValid,
        [requestId, responder](const ArtworkInteractionResult& result) {
            if (result.status != ArtworkInteractionResult::kOk)
            {
                responder.send(failure(result, requestId));
                return;
            }
            std::ostringstream output;
            output << "{\"commentId\":" << result.commentId << '}';
            responder.send(makeApiSuccess(output.str()));
        });
}

} // namespace ar
