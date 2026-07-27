// 场景互动业务服务：协调 DAO 的异步点赞与评论持久化，并定义业务级失败语义。
#pragma once

#include <db/SceneInteractionDAO.h>

#include <services/SessionService.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ar {

struct SceneInteractionResult
{
    enum Status { kOk, kUnauthorized, kBadRequest, kNotFound, kUnavailable };

    SceneInteractionResult()
        : status(kOk), likeCount(0), liked(false), nextBefore(0) {}

    Status status;
    uint64_t likeCount;
    bool liked;
    std::vector<SceneComment> comments;
    uint64_t nextBefore;
};

class SceneInteractionService
{
public:
    typedef std::function<void(const SceneInteractionResult&)> Callback;

    SceneInteractionService(SessionService* sessions, SceneInteractionDAO* dao)
        : sessions_(sessions), dao_(dao) {}

    void detail(const std::string& sceneId, const Callback& callback) const;
    void like(const std::string& token, const std::string& sceneId, const Callback& callback) const;
    void unlike(const std::string& token, const std::string& sceneId, const Callback& callback) const;
    void listComments(const std::string& sceneId, uint64_t beforeId, uint32_t limit,
                      const Callback& callback) const;
    void comment(const std::string& token, const std::string& sceneId, const std::string& content,
                 const Callback& callback) const;

private:
    typedef std::function<void(uint64_t)> AuthenticatedCallback;

    bool sceneExists(const std::string& sceneId, const Callback& callback) const;
    void authenticate(const std::string& token, const Callback& failure,
                      const AuthenticatedCallback& success) const;
    static SceneInteractionResult result(SceneInteractionResult::Status status);

    SessionService* sessions_;
    SceneInteractionDAO* dao_;
};

} // namespace ar
