#include "TestSupport.h"

#include <utils/JsonUtil.h>
#include <catalog/ExhibitionCatalog.h>
#include <handlers/AuthHandler.h>
#include <services/AuthService.h>
#include <http/AsyncResponder.h>
#include <services/SessionService.h>
#include <handlers/SessionHandlers.h>
#include <handlers/SceneHandlers.h>
#include <handlers/SceneInteractionHandlers.h>
#include <services/SceneInteractionService.h>
#include <utils/ApiError.h>
#include <services/ArSessionValidator.h>
#include <base/TaskWorkerPool.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <cctype>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

class FakeAuthStore : public ar::AuthStore
{
public:
    void findUser(const std::string&, const UserCallback& callback) override
    { callback(std::shared_ptr<User>(new User(7, "alice", ar::AuthService::passwordHash("secret")))); }
    void createUser(const std::string&, const std::string&, const IdCallback& callback) override
    { callback(0); }
    void createSession(uint64_t, const std::string& token, const IdCallback& callback) override
    { createdToken = token; callback(8); }
    std::string createdToken;
};

class FakeSessionStore : public ar::SessionStore
{
public:
    void find(const std::string&, const SessionCallback& callback) override
    { callback(std::shared_ptr<Session>(new Session(9, "token", 7, "scene-a", 0))); }
    void enter(uint64_t, const std::string&, const BoolCallback& callback) override { callback(true); }
    void exit(uint64_t, const BoolCallback& callback) override { callback(true); }
};

class MissingSessionStore : public ar::SessionStore
{
public:
    void find(const std::string&, const SessionCallback& callback) override
    { callback(std::shared_ptr<Session>()); }
    void enter(uint64_t, const std::string&, const BoolCallback& callback) override { callback(false); }
    void exit(uint64_t, const BoolCallback& callback) override { callback(false); }
};

int main()
{
    std::string username;
    std::string password;
    HttpRequest bodyCredentials;
    bodyCredentials.setBody("{\"username\":\"alice\",\"password\":\"secret\"}");
    CHECK(ar::AuthHandler::credentials(bodyCredentials, &username, &password));
    CHECK(username == "alice");
    CHECK(password == "secret");
    HttpRequest queryCredentials;
    queryCredentials.setQuery("username=alice&password=secret");
    CHECK(ar::AuthHandler::credentials(queryCredentials, &username, &password));
    HttpRequest preferredBodyCredentials;
    preferredBodyCredentials.setBody("{\"username\":\"body-user\",\"password\":\"body-password\"}");
    preferredBodyCredentials.setQuery("username=query-user&password=query-password");
    CHECK(ar::AuthHandler::credentials(preferredBodyCredentials, &username, &password));
    CHECK(username == "body-user");
    CHECK(password == "body-password");
    CHECK(ar::AuthService::passwordHash("secret") ==
          "sha256:2bb80d537b1da3e38bd30361aa855686bde0eacd7162fef6a25fe97bf527a25b");
    CHECK(ar::JsonUtil::escape("quote\" slash\\ control\n") ==
          "quote\\\" slash\\\\ control\\n");
    HttpRequest request;
    request.setQuery("username=alice");
    HttpResponse response(false);
    CHECK(!ar::AuthHandler::validate(request, &response));
    CHECK(response.statusCode() == HttpResponse::k400BadRequest);
    CHECK(response.body() ==
          "{\"success\":false,\"data\":null,\"message\":\"missing username or password\","
          "\"code\":\"INVALID_CREDENTIALS\",\"requestId\":\"\"}");
    ar::AuthResult result;
    result.username = "alice";
    result.userId = 7;
    result.sessionToken = "token";
    result.isNew = true;
    CHECK(ar::AuthService::json(result) ==
          "{\"isNew\":true,\"username\":\"alice\",\"userId\":7,\"token\":\"token\"}");
    FakeAuthStore store;
    ar::AuthService service(&store);
    bool completed = false;
    service.authenticate("alice", "wrong", [&](const ar::AuthResult& auth, int status) {
        CHECK(auth.sessionToken.empty());
        CHECK(status == 401);
        completed = true;
    });
    CHECK(completed);
    service.authenticate("alice", "secret", [&](const ar::AuthResult& auth, int status) {
        CHECK(status == 200);
        CHECK(auth.sessionToken == store.createdToken);
        CHECK(auth.sessionToken.size() == 64);
        for (size_t index = 0; index < auth.sessionToken.size(); ++index)
            CHECK(std::isxdigit(static_cast<unsigned char>(auth.sessionToken[index])));
    });
    HttpRequest authRequest;
    authRequest.setQuery("username=alice&password=wrong");
    HttpResponse asyncResponse(false);
    ar::AuthHandler handler(&service);
    handler.handle(authRequest, AsyncResponder([&](HttpResponse response) { asyncResponse = response; }));
    CHECK(asyncResponse.statusCode() == HttpResponse::k401Unauthorized);
    CHECK(asyncResponse.body() ==
          "{\"success\":false,\"data\":null,\"message\":\"invalid password\","
          "\"code\":\"INVALID_PASSWORD\",\"requestId\":\"\"}");
    HttpRequest jsonAuthRequest;
    jsonAuthRequest.setBody("{\"username\":\"alice\",\"password\":\"secret\"}");
    HttpResponse jsonAuthResponse(false);
    handler.handle(jsonAuthRequest, AsyncResponder([&](HttpResponse response) { jsonAuthResponse = response; }));
    CHECK(jsonAuthResponse.statusCode() == HttpResponse::k200Ok);
    CHECK(jsonAuthResponse.body().find("\"success\":true") != std::string::npos);
    CHECK(jsonAuthResponse.body().find("\"username\":\"alice\"") != std::string::npos);
    CHECK(jsonAuthResponse.body().find("\"userId\":7") != std::string::npos);
    CHECK(jsonAuthResponse.body().find("\"token\":") != std::string::npos);
    FakeSessionStore sessionStore;
    ar::SessionService sessionService(&sessionStore);
    bool found = false;
    sessionService.get("token", [&](const std::shared_ptr<Session>& session) {
        CHECK(session && session->status == 0);
        found = true;
    });
    CHECK(found);
    ar::ArSessionValidator validator(&sessionService);
    bool validated = true;
    validator.validate("token", [&](bool valid) { validated = valid; });
    CHECK(!validated);
    bool entered = false;
    sessionService.enter("token", "scene-b", [&](bool ok) { CHECK(ok); entered = true; });
    CHECK(entered);
    Session exited(9, "token", 7, "scene-a", 0, "created", "updated");
    CHECK(ar::SessionHandlers::json(exited) ==
          "{\"status\":\"ok\",\"session_id\":9,\"token\":\"token\",\"user_id\":7,\"scene_id\":\"scene-a\",\"status_code\":0,\"created_at\":\"created\",\"updated_at\":\"updated\"}");
    ar::SessionHandlers sessionHandlers(&sessionService);
    HttpRequest sessionRequest;
    sessionRequest.setQuery("token=token");
    HttpResponse sessionResponse(false);
    sessionHandlers.get(sessionRequest, AsyncResponder([&](HttpResponse response) { sessionResponse = response; }));
    CHECK(sessionResponse.statusCode() == HttpResponse::k200Ok);

    MissingSessionStore missingStore;
    ar::SessionService missingSessionService(&missingStore);
    ar::SessionHandlers missingSessionHandlers(&missingSessionService);
    HttpRequest expiredEnterRequest;
    expiredEnterRequest.setQuery("token=expired-token&scene=golden-bay");
    HttpResponse expiredEnterResponse(false);
    missingSessionHandlers.enter(expiredEnterRequest,
        AsyncResponder([&](HttpResponse response) { expiredEnterResponse = response; }));
    CHECK(expiredEnterResponse.statusCode() == HttpResponse::k401Unauthorized);
    CHECK(expiredEnterResponse.body() ==
          "{\"success\":false,\"data\":null,\"message\":\"session expired\","
          "\"code\":\"SESSION_EXPIRED\",\"requestId\":\"\"}");

    TaskWorkerPool delayedWorkers(1, 4);
    ar::SessionHandlers delayedHandlers(&sessionService, 0, &delayedWorkers, 20);
    std::atomic_bool delayedComplete(false);
    delayedHandlers.get(sessionRequest, AsyncResponder([&](HttpResponse response) {
        CHECK(response.statusCode() == HttpResponse::k200Ok);
        delayedComplete.store(true);
    }));
    CHECK(!delayedComplete.load());
    for (int attempt = 0; attempt < 20 && !delayedComplete.load(); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(delayedComplete.load());
    std::vector<std::string> catalogErrors;
    std::unique_ptr<ar::ExhibitionCatalog> catalog =
        ar::ExhibitionCatalog::load("WebApps/ARServer/config/exhibition.json",
                                    "WebApps/ARServer/www", &catalogErrors);
    CHECK(catalog.get() != 0);
    CHECK(catalogErrors.empty());
    ar::SceneHandlers sceneHandlers(catalog.get());

    HttpResponse scenesResponse(false);
    sceneHandlers.list(HttpRequest(), &scenesResponse);
    CHECK(scenesResponse.statusCode() == HttpResponse::k200Ok);
    CHECK(scenesResponse.header("Content-Type") == "application/json; charset=utf-8");
    nlohmann::json scenesJson = nlohmann::json::parse(scenesResponse.body());
    CHECK(scenesJson["success"].get<bool>());
    CHECK(scenesJson["message"].get<std::string>().empty());
    CHECK(scenesJson["data"]["title"].get<std::string>() == catalog->title());
    CHECK(scenesJson["data"]["remark"].get<std::string>() == catalog->remark());
    CHECK(scenesJson["data"]["defaultSceneId"].get<std::string>() == "76196992");
    CHECK(scenesJson["data"]["scenes"].size() == 9);
    CHECK(scenesJson["data"]["scenes"][0]["sceneId"].get<std::string>() == "76196992");
    CHECK(scenesJson["data"]["scenes"][0]["panoId"].get<std::string>() == "15949056");
    CHECK(scenesJson["data"]["scenes"][0]["name"].get<std::string>() == "展厅入口");
    CHECK(scenesJson["data"]["scenes"][0]["previewUrl"].get<std::string>() ==
          "/assets/pano/15949056/preview.jpg");
    CHECK(scenesJson["data"]["scenes"][0]["thumbnailUrl"].get<std::string>() ==
          "/assets/pano/15949056/thumb.jpg");
    CHECK(scenesJson["data"]["scenes"][0].find("hotspots") ==
          scenesJson["data"]["scenes"][0].end());

    HttpRequest sceneRequest;
    sceneRequest.setPathParameter("sceneId", "76196996");
    HttpResponse sceneResponse(false);
    sceneHandlers.get(sceneRequest, &sceneResponse);
    CHECK(sceneResponse.statusCode() == HttpResponse::k200Ok);
    nlohmann::json sceneJson = nlohmann::json::parse(sceneResponse.body());
    CHECK(sceneJson["success"].get<bool>());
    const nlohmann::json& sceneData = sceneJson["data"];
    CHECK(sceneData["sceneId"].get<std::string>() == "76196996");
    CHECK(sceneData["cubeUrl"].get<std::string>() ==
          "/assets/pano/15949052/15949052_%s.jpg");
    CHECK(sceneData["previewUrl"].get<std::string>() ==
          "/assets/pano/15949052/preview.jpg");
    CHECK(sceneData["view"]["hlookat"].is_number());
    CHECK(sceneData["view"]["vlookat"].is_number());
    CHECK(sceneData["view"]["fov"].get<double>() == 110.0);
    CHECK(sceneData["music"]["url"].get<std::string>() ==
          "/assets/music/musicword-reading-books-312690.mp3");
    CHECK(sceneData["music"]["volume"].get<double>() == 0.05);
    CHECK(sceneData["music"]["autoplay"].get<bool>());
    CHECK(sceneData["music"]["loop"].get<bool>());
    CHECK(sceneData["hotspots"].size() == catalog->findScene("76196996")->hotspots.size());

    bool foundInactive = false;
    for (nlohmann::json::const_iterator it = sceneData["hotspots"].begin();
         it != sceneData["hotspots"].end(); ++it)
    {
        if ((*it)["type"].get<std::string>() == "inactive")
        {
            foundInactive = true;
            CHECK((*it)["hotspotId"].get<std::string>() == "s_76196996_4");
            CHECK(!(*it)["renderable"].get<bool>());
        }
        else
        {
            CHECK((*it)["renderable"].get<bool>());
        }
    }
    CHECK(foundInactive);

    HttpRequest unknownSceneRequest;
    unknownSceneRequest.setPathParameter("sceneId", "unknown");
    HttpResponse unknownSceneResponse(false);
    sceneHandlers.get(unknownSceneRequest, &unknownSceneResponse);
    CHECK(unknownSceneResponse.statusCode() == HttpResponse::k404NotFound);
    CHECK(unknownSceneResponse.header("Content-Type") == "application/json; charset=utf-8");
    CHECK(unknownSceneResponse.body() ==
          "{\"success\":false,\"data\":null,\"message\":\"scene not found\","
          "\"code\":\"SCENE_NOT_FOUND\",\"requestId\":\"\"}");
    HttpResponse apiError = ar::makeApiError(HttpResponse::k503ServiceUnavailable,
                                             "SERVICE_UNAVAILABLE", "cache unavailable", "request-1");
    CHECK(apiError.statusCode() == HttpResponse::k503ServiceUnavailable);
    CHECK(apiError.body() ==
          "{\"success\":false,\"data\":null,\"message\":\"cache unavailable\","
          "\"code\":\"SERVICE_UNAVAILABLE\",\"requestId\":\"request-1\"}");
    HttpResponse interactionResponse(false);
    ar::SceneHandlers::interactions(HttpRequest(), &interactionResponse);
    CHECK(interactionResponse.statusCode() == HttpResponse::k501NotImplemented);
    CHECK(interactionResponse.body().find("INTERACTIONS_NOT_IMPLEMENTED") != std::string::npos);
    ar::SceneInteractionService interactionService(&sessionService, 0);
    ar::SceneInteractionHandlers interactionHandlers(&interactionService);
    HttpRequest commentRequest;
    commentRequest.setPathParameter("sceneId", "golden-bay");
    commentRequest.setBody("{\"content\":\"hello\"}");
    HttpResponse commentResponse(false);
    interactionHandlers.comment(commentRequest,
        AsyncResponder([&](HttpResponse response) { commentResponse = response; }));
    CHECK(commentResponse.statusCode() == HttpResponse::k401Unauthorized);
    return 0;
}
