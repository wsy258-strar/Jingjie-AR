#include "TestSupport.h"

#include <catalog/ExhibitionCatalog.h>
#include <handlers/ArtworkInteractionHandlers.h>
#include <services/ArtworkInteractionService.h>
#include <services/SessionService.h>

#include <memory>

namespace {

class ImmediateSessionStore : public ar::SessionStore
{
public:
    void find(const std::string& token, const SessionCallback& callback) override
    {
        if (token == "valid-token")
            callback(std::shared_ptr<Session>(new Session(1, token, 7, "", 1)));
        else if (token == "inactive-token")
            callback(std::shared_ptr<Session>(new Session(2, token, 8, "", 0)));
        else
            callback(std::shared_ptr<Session>());
    }
    void enter(uint64_t, const std::string&, const BoolCallback& callback) override { callback(false); }
    void exit(uint64_t, const BoolCallback& callback) override { callback(false); }
};

class ImmediateDAO : public ArtworkInteractionDAO
{
public:
    ImmediateDAO() : ArtworkInteractionDAO(0) {}

    void like(const std::string&, uint64_t, const LikeCallback& callback) override
    {
        callback(true, true, 4, true);
    }
    void unlike(const std::string&, uint64_t, const LikeCallback& callback) override
    {
        callback(true, true, 3, false);
    }
    void summary(const std::string&, uint64_t userId, const SummaryCallback& callback) override
    {
        callback(true, 3, userId != 0);
    }
    void createComment(const std::string&, uint64_t, const std::string&,
                       const CommentCallback& callback) override
    {
        callback(true, 42);
    }
    void listComments(const std::string&, uint64_t, uint32_t,
                      const CommentsCallback& callback) override
    {
        ArtworkComment comment;
        comment.id = 8;
        comment.username = "a\"lice";
        comment.content = "line one\n</script>";
        std::vector<ArtworkComment> comments(1, comment);
        callback(true, comments, 8);
    }
};

std::unique_ptr<ar::ExhibitionCatalog> loadCatalog()
{
    std::vector<std::string> errors;
    std::unique_ptr<ar::ExhibitionCatalog> catalog = ar::ExhibitionCatalog::load(
        "WebApps/ARServer/config/exhibition.json",
        "WebApps/ARServer/www",
        &errors);
    CHECK(catalog.get() != 0);
    return catalog;
}

std::string knownArtworkId(const ar::ExhibitionCatalog& catalog)
{
    const std::vector<ar::ExhibitionScene>& scenes = catalog.scenes();
    for (size_t scene = 0; scene < scenes.size(); ++scene)
        for (size_t hotspot = 0; hotspot < scenes[scene].hotspots.size(); ++hotspot)
            if (!scenes[scene].hotspots[hotspot].artworkId.empty())
                return scenes[scene].hotspots[hotspot].artworkId;
    CHECK(false);
    return std::string();
}

HttpResponse invoke(const std::function<void(const AsyncResponder&)>& handler)
{
    HttpResponse response(false);
    handler(AsyncResponder([&response](HttpResponse value) { response = value; }));
    return response;
}

void testAllSuccessfulHandlersUseUnifiedEnvelopeAndEscapeJson()
{
    std::unique_ptr<ar::ExhibitionCatalog> catalog = loadCatalog();
    ImmediateSessionStore store;
    ar::SessionService sessions(&store);
    ImmediateDAO dao;
    ar::ArtworkInteractionService service(catalog.get(), &sessions, &dao);
    ar::ArtworkInteractionHandlers handlers(&service);
    const std::string artworkId = knownArtworkId(*catalog);
    HttpRequest request;
    request.setPathParameter("artworkId", artworkId);
    request.setAttribute("auth.token", "valid-token");

    HttpResponse detail = invoke([&](const AsyncResponder& responder) {
        handlers.detail(request, responder);
    });
    CHECK(detail.statusCode() == HttpResponse::k200Ok);
    CHECK(detail.body().find("{\"success\":true,\"data\":") == 0);
    CHECK(detail.body().find("\"artworkId\":\"" + artworkId + "\"") != std::string::npos);
    CHECK(detail.body().find("\"likeCount\":3") != std::string::npos);
    CHECK(detail.body().find("\"liked\":true") != std::string::npos);

    HttpResponse like = invoke([&](const AsyncResponder& responder) {
        handlers.like(request, responder);
    });
    CHECK(like.body() ==
          "{\"success\":true,\"data\":{\"liked\":true,\"likeCount\":4},\"message\":\"\"}");

    HttpResponse unlike = invoke([&](const AsyncResponder& responder) {
        handlers.unlike(request, responder);
    });
    CHECK(unlike.body() ==
          "{\"success\":true,\"data\":{\"liked\":false,\"likeCount\":3},\"message\":\"\"}");

    HttpResponse comments = invoke([&](const AsyncResponder& responder) {
        handlers.comments(request, responder);
    });
    CHECK(comments.body().find("{\"success\":true,\"data\":") == 0);
    CHECK(comments.body().find("\"username\":\"a\\\"lice\"") != std::string::npos);
    CHECK(comments.body().find("\"content\":\"line one\\n</script>\"") != std::string::npos);

    request.setBody("{\"content\":\"hello\"}");
    HttpResponse comment = invoke([&](const AsyncResponder& responder) {
        handlers.comment(request, responder);
    });
    CHECK(comment.body() ==
          "{\"success\":true,\"data\":{\"commentId\":42},\"message\":\"\"}");
}

void testFailuresUseUnifiedErrorEnvelope()
{
    std::unique_ptr<ar::ExhibitionCatalog> catalog = loadCatalog();
    ImmediateSessionStore store;
    ar::SessionService sessions(&store);
    ImmediateDAO dao;
    ar::ArtworkInteractionService service(catalog.get(), &sessions, &dao);
    ar::ArtworkInteractionHandlers handlers(&service);
    HttpRequest request;
    request.setPathParameter("artworkId", "missing-artwork");
    request.setAttribute("request_id", "request-4");

    HttpResponse response = invoke([&](const AsyncResponder& responder) {
        handlers.like(request, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k404NotFound);
    CHECK(response.body().find("\"success\":false") != std::string::npos);
    CHECK(response.body().find("\"code\":\"ARTWORK_NOT_FOUND\"") != std::string::npos);
    CHECK(response.body().find("\"requestId\":\"request-4\"") != std::string::npos);

    request.setBody("{not json}");
    response = invoke([&](const AsyncResponder& responder) {
        handlers.comment(request, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k404NotFound);
    CHECK(response.body().find("\"code\":\"ARTWORK_NOT_FOUND\"") != std::string::npos);

    request.setPathParameter("artworkId", knownArtworkId(*catalog));
    response = invoke([&](const AsyncResponder& responder) {
        handlers.comment(request, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k401Unauthorized);
    CHECK(response.body().find("\"code\":\"UNAUTHORIZED\"") != std::string::npos);

    request.setAttribute("auth.token", "valid-token");
    response = invoke([&](const AsyncResponder& responder) {
        handlers.comment(request, responder);
    });
    CHECK(response.statusCode() == HttpResponse::k400BadRequest);
    CHECK(response.body().find("\"code\":\"INVALID_COMMENT\"") != std::string::npos);
    CHECK(response.body().find("\"success\":false") != std::string::npos);
}

void testInactiveBearerReturnsUnauthorizedForOptionalDetailAndProtectedWrite()
{
    std::unique_ptr<ar::ExhibitionCatalog> catalog = loadCatalog();
    ImmediateSessionStore store;
    ar::SessionService sessions(&store);
    ImmediateDAO dao;
    ar::ArtworkInteractionService service(catalog.get(), &sessions, &dao);
    ar::ArtworkInteractionHandlers handlers(&service);
    HttpRequest request;
    request.setPathParameter("artworkId", knownArtworkId(*catalog));
    request.setAttribute("auth.token", "inactive-token");
    request.setAttribute("request_id", "inactive-session");

    const HttpResponse detail = invoke([&](const AsyncResponder& responder) {
        handlers.detail(request, responder);
    });
    CHECK(detail.statusCode() == HttpResponse::k401Unauthorized);
    CHECK(detail.body().find("\"code\":\"UNAUTHORIZED\"") != std::string::npos);

    const HttpResponse like = invoke([&](const AsyncResponder& responder) {
        handlers.like(request, responder);
    });
    CHECK(like.statusCode() == HttpResponse::k401Unauthorized);
    CHECK(like.body().find("\"code\":\"UNAUTHORIZED\"") != std::string::npos);
}

} // namespace

int main()
{
    testAllSuccessfulHandlersUseUnifiedEnvelopeAndEscapeJson();
    testFailuresUseUnifiedErrorEnvelope();
    testInactiveBearerReturnsUnauthorizedForOptionalDetailAndProtectedWrite();
    return 0;
}
