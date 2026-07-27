// 场景互动数据访问对象：以参数绑定执行点赞和评论持久化，避免业务层直接操作 MySQL C API。
#pragma once

#include <base/noncopyable.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct SceneComment
{
    uint64_t id;
    std::string username;
    std::string content;
};

class SceneInteractionDAO : noncopyable
{
public:
    typedef std::function<void(bool ok, bool changed, uint64_t count)> LikeCallback;
    typedef std::function<void(bool ok, uint64_t id)> CommentCallback;
    typedef std::function<void(bool ok, uint64_t count)> SummaryCallback;
    typedef std::function<void(bool ok, const std::vector<SceneComment>& comments, uint64_t nextBefore)> CommentsCallback;

    explicit SceneInteractionDAO(class DBWorkerPool* dbPool) : dbPool_(dbPool) {}

    void like(const std::string& sceneId, uint64_t userId, const LikeCallback& callback);
    void unlike(const std::string& sceneId, uint64_t userId, const LikeCallback& callback);
    void createComment(const std::string& sceneId, uint64_t userId,
                       const std::string& content, const CommentCallback& callback);
    void summary(const std::string& sceneId, const SummaryCallback& callback);
    void listComments(const std::string& sceneId, uint64_t beforeId, uint32_t limit,
                      const CommentsCallback& callback);

private:
    class DBWorkerPool* dbPool_;
};
