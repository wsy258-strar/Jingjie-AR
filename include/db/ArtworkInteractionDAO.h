// 作品互动数据访问对象：使用异步数据库工作池持久化作品点赞和评论。
#pragma once

#include <base/noncopyable.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct ArtworkComment
{
    uint64_t id;
    std::string username;
    std::string content;
};

class ArtworkInteractionDAO : noncopyable
{
public:
    typedef std::function<void(bool ok, bool changed, uint64_t count)> LikeCallback;
    typedef std::function<void(bool ok, uint64_t id)> CommentCallback;
    typedef std::function<void(bool ok, uint64_t count, bool liked)> SummaryCallback;
    typedef std::function<void(bool ok, const std::vector<ArtworkComment>& comments,
                               uint64_t nextBefore)> CommentsCallback;

    explicit ArtworkInteractionDAO(class DBWorkerPool* dbPool) : dbPool_(dbPool) {}
    virtual ~ArtworkInteractionDAO() {}

    virtual void like(const std::string& artworkId, uint64_t userId,
                      const LikeCallback& callback);
    virtual void unlike(const std::string& artworkId, uint64_t userId,
                        const LikeCallback& callback);
    virtual void summary(const std::string& artworkId, uint64_t optionalUserId,
                         const SummaryCallback& callback);
    virtual void createComment(const std::string& artworkId, uint64_t userId,
                               const std::string& content, const CommentCallback& callback);
    virtual void listComments(const std::string& artworkId, uint64_t beforeId, uint32_t limit,
                              const CommentsCallback& callback);

private:
    class DBWorkerPool* dbPool_;
};
