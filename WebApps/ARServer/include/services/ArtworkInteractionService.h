// 作品互动业务服务：校验展馆作品、用户身份和评论输入后编排异步 DAO 操作。
#pragma once

#include <db/ArtworkInteractionDAO.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ar {

class ExhibitionCatalog;
struct ArtworkInfo;
class SessionService;

struct ArtworkInteractionResult
{
    enum Status { kOk, kUnauthorized, kBadRequest, kNotFound, kUnavailable };

    ArtworkInteractionResult()
        : status(kOk), likeCount(0), commentCount(0), liked(false), nextBefore(0), commentId(0) {}

    Status status;
    uint64_t likeCount;
    uint64_t commentCount;
    bool liked;
    std::vector<ArtworkComment> comments;
    uint64_t nextBefore;
    uint64_t commentId;
};

class ArtworkInteractionService
{
public:
    typedef std::function<void(const ArtworkInteractionResult&)> Callback;

    ArtworkInteractionService(const ExhibitionCatalog* catalog, SessionService* sessions,
                              ArtworkInteractionDAO* dao);
    ~ArtworkInteractionService();
    ArtworkInteractionService(const ArtworkInteractionService&) = delete;
    ArtworkInteractionService& operator=(const ArtworkInteractionService&) = delete;

    const ArtworkInfo* findArtwork(const std::string& artworkId) const;
    void detail(const std::string& token, const std::string& artworkId,
                const Callback& callback) const;
    void like(const std::string& token, const std::string& artworkId,
              const Callback& callback) const;
    void unlike(const std::string& token, const std::string& artworkId,
                const Callback& callback) const;
    void listComments(const std::string& artworkId, uint64_t beforeId, uint32_t limit,
                      const Callback& callback) const;
    void comment(const std::string& token, const std::string& artworkId,
                 const std::string& content, const Callback& callback) const;
    void commentRequest(const std::string& token, const std::string& artworkId,
                        const std::string& content, bool contentValid,
                        const Callback& callback) const;

private:
    typedef std::function<void(uint64_t)> AuthenticatedCallback;
    struct Lifetime;
    class DaoCallLease;

    bool artworkExists(const std::string& artworkId, const Callback& callback) const;
    void authenticate(const std::string& token, const Callback& failure,
                      const AuthenticatedCallback& success) const;
    void loadSummary(const std::string& artworkId, uint64_t userId,
                     const Callback& callback) const;
    static bool validNonBlankUtf8(const std::string& content);
    static ArtworkInteractionResult result(ArtworkInteractionResult::Status status);
    static bool active(const std::weak_ptr<Lifetime>& lifetime);

    const ExhibitionCatalog* catalog_;
    SessionService* sessions_;
    std::shared_ptr<Lifetime> lifetime_;
};

} // namespace ar
