#include "TestSupport.h"

#include <services/SceneInteractionService.h>

#include <memory>

namespace {

class ImmediateSessionStore : public ar::SessionStore
{
public:
    void find(const std::string& token, const SessionCallback& callback) override
    {
        if (token == "valid-token")
            callback(std::shared_ptr<Session>(new Session(1, token, 7, "golden-bay", 1)));
        else
            callback(std::shared_ptr<Session>());
    }

    void enter(uint64_t, const std::string&, const BoolCallback& callback) override { callback(false); }
    void exit(uint64_t, const BoolCallback& callback) override { callback(false); }
};

void testCommentRejectsEmptyContentBeforeDatabaseWork()
{
    ImmediateSessionStore store;
    ar::SessionService sessions(&store);
    ar::SceneInteractionService service(&sessions, 0);
    ar::SceneInteractionResult result;

    service.comment("valid-token", "golden-bay", "", [&result](const ar::SceneInteractionResult& value) {
        result = value;
    });

    CHECK(result.status == ar::SceneInteractionResult::kBadRequest);
}

void testLikeRejectsMissingToken()
{
    ImmediateSessionStore store;
    ar::SessionService sessions(&store);
    ar::SceneInteractionService service(&sessions, 0);
    ar::SceneInteractionResult result;

    service.like("", "golden-bay", [&result](const ar::SceneInteractionResult& value) {
        result = value;
    });

    CHECK(result.status == ar::SceneInteractionResult::kUnauthorized);
}

void testCommentRejectsUnknownScene()
{
    ImmediateSessionStore store;
    ar::SessionService sessions(&store);
    ar::SceneInteractionService service(&sessions, 0);
    ar::SceneInteractionResult result;

    service.comment("valid-token", "not-a-scene", "hello", [&result](const ar::SceneInteractionResult& value) {
        result = value;
    });

    CHECK(result.status == ar::SceneInteractionResult::kNotFound);
}

} // namespace

int main()
{
    testCommentRejectsEmptyContentBeforeDatabaseWork();
    testLikeRejectsMissingToken();
    testCommentRejectsUnknownScene();
    return 0;
}
