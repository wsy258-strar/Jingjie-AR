#include <services/SceneInteractionService.h>

#include <catalog/SceneCatalog.h>

#include <memory>

namespace ar {

SceneInteractionResult SceneInteractionService::result(SceneInteractionResult::Status status)
{
    SceneInteractionResult value;
    value.status = status;
    return value;
}

bool SceneInteractionService::sceneExists(const std::string& sceneId, const Callback& callback) const
{
    if (SceneCatalog::find(sceneId)) return true;
    callback(result(SceneInteractionResult::kNotFound));
    return false;
}

void SceneInteractionService::authenticate(const std::string& token, const Callback& failure,
                                           const AuthenticatedCallback& success) const
{
    if (token.empty() || !sessions_)
    {
        failure(result(token.empty() ? SceneInteractionResult::kUnauthorized
                                     : SceneInteractionResult::kUnavailable));
        return;
    }
    sessions_->get(token, [failure, success](const std::shared_ptr<Session>& session) {
        if (!session || session->status == 0)
        {
            failure(result(SceneInteractionResult::kUnauthorized));
            return;
        }
        success(session->userId);
    });
}

void SceneInteractionService::detail(const std::string& sceneId, const Callback& callback) const
{
    if (!sceneExists(sceneId, callback)) return;
    if (!dao_)
    {
        callback(result(SceneInteractionResult::kUnavailable));
        return;
    }
    dao_->summary(sceneId, [callback](bool ok, uint64_t count) {
        if (!ok) { callback(result(SceneInteractionResult::kUnavailable)); return; }
        SceneInteractionResult value;
        value.likeCount = count;
        callback(value);
    });
}

void SceneInteractionService::like(const std::string& token, const std::string& sceneId,
                                   const Callback& callback) const
{
    if (!sceneExists(sceneId, callback)) return;
    authenticate(token, callback, [this, sceneId, callback](uint64_t userId) {
        if (!dao_)
        {
            callback(result(SceneInteractionResult::kUnavailable));
            return;
        }
        dao_->like(sceneId, userId, [callback](bool ok, bool, uint64_t count) {
            if (!ok) { callback(result(SceneInteractionResult::kUnavailable)); return; }
            SceneInteractionResult value;
            value.likeCount = count;
            value.liked = true;
            callback(value);
        });
    });
}

void SceneInteractionService::unlike(const std::string& token, const std::string& sceneId,
                                     const Callback& callback) const
{
    if (!sceneExists(sceneId, callback)) return;
    authenticate(token, callback, [this, sceneId, callback](uint64_t userId) {
        if (!dao_)
        {
            callback(result(SceneInteractionResult::kUnavailable));
            return;
        }
        dao_->unlike(sceneId, userId, [callback](bool ok, bool, uint64_t count) {
            if (!ok) { callback(result(SceneInteractionResult::kUnavailable)); return; }
            SceneInteractionResult value;
            value.likeCount = count;
            value.liked = false;
            callback(value);
        });
    });
}

void SceneInteractionService::listComments(const std::string& sceneId, uint64_t beforeId,
                                           uint32_t limit, const Callback& callback) const
{
    if (!sceneExists(sceneId, callback)) return;
    if (!dao_)
    {
        callback(result(SceneInteractionResult::kUnavailable));
        return;
    }
    if (limit == 0) limit = 20;
    if (limit > 20) limit = 20;
    dao_->listComments(sceneId, beforeId, limit, [callback](bool ok, const std::vector<SceneComment>& comments,
                                                             uint64_t nextBefore) {
        if (!ok) { callback(result(SceneInteractionResult::kUnavailable)); return; }
        SceneInteractionResult value;
        value.comments = comments;
        value.nextBefore = nextBefore;
        callback(value);
    });
}

void SceneInteractionService::comment(const std::string& token, const std::string& sceneId,
                                      const std::string& content, const Callback& callback) const
{
    if (!sceneExists(sceneId, callback)) return;
    if (content.empty() || content.size() > 300)
    {
        callback(result(SceneInteractionResult::kBadRequest));
        return;
    }
    authenticate(token, callback, [this, sceneId, content, callback](uint64_t userId) {
        if (!dao_)
        {
            callback(result(SceneInteractionResult::kUnavailable));
            return;
        }
        dao_->createComment(sceneId, userId, content, [callback](bool ok, uint64_t id) {
            if (!ok || id == 0)
            {
                callback(result(SceneInteractionResult::kUnavailable));
                return;
            }
            SceneInteractionResult value;
            value.nextBefore = id;
            callback(value);
        });
    });
}

} // namespace ar
