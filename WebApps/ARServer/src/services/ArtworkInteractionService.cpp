// 作品点赞和评论业务编排实现：写操作严格按作品存在、用户身份、业务输入的顺序校验。
#include <services/ArtworkInteractionService.h>

#include <catalog/ExhibitionCatalog.h>
#include <services/SessionService.h>

#include <condition_variable>
#include <memory>
#include <mutex>

namespace ar {
namespace {

bool nextCodePoint(const std::string& text, size_t* position, uint32_t* codePoint)
{
    const unsigned char lead = static_cast<unsigned char>(text[*position]);
    ++*position;
    if (lead <= 0x7f)
    {
        *codePoint = lead;
        return true;
    }

    size_t continuationCount = 0;
    uint32_t value = 0;
    uint32_t minimum = 0;
    if (lead >= 0xc2 && lead <= 0xdf)
    {
        continuationCount = 1;
        value = lead & 0x1f;
        minimum = 0x80;
    }
    else if (lead >= 0xe0 && lead <= 0xef)
    {
        continuationCount = 2;
        value = lead & 0x0f;
        minimum = 0x800;
    }
    else if (lead >= 0xf0 && lead <= 0xf4)
    {
        continuationCount = 3;
        value = lead & 0x07;
        minimum = 0x10000;
    }
    else
    {
        return false;
    }

    if (text.size() - *position < continuationCount) return false;
    for (size_t index = 0; index < continuationCount; ++index)
    {
        const unsigned char byte = static_cast<unsigned char>(text[*position]);
        if ((byte & 0xc0) != 0x80) return false;
        value = (value << 6) | (byte & 0x3f);
        ++*position;
    }
    if (value < minimum || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff))
        return false;
    *codePoint = value;
    return true;
}

bool unicodeWhitespace(uint32_t codePoint)
{
    return (codePoint >= 0x0009 && codePoint <= 0x000d) ||
           codePoint == 0x0020 || codePoint == 0x0085 || codePoint == 0x00a0 ||
           codePoint == 0x1680 || (codePoint >= 0x2000 && codePoint <= 0x200a) ||
           codePoint == 0x2028 || codePoint == 0x2029 || codePoint == 0x202f ||
           codePoint == 0x205f || codePoint == 0x3000;
}

} // namespace

struct ArtworkInteractionService::Lifetime
{
    explicit Lifetime(ArtworkInteractionDAO* value)
        : valid(true), activeCalls(0), dao(value) {}

    std::mutex mutex;
    std::condition_variable drained;
    bool valid;
    size_t activeCalls;
    ArtworkInteractionDAO* dao;
};

class ArtworkInteractionService::DaoCallLease
{
public:
    explicit DaoCallLease(const std::weak_ptr<Lifetime>& weakLifetime)
        : dao_(0)
    {
        const std::shared_ptr<Lifetime> lifetime = weakLifetime.lock();
        if (!lifetime) return;
        std::lock_guard<std::mutex> lock(lifetime->mutex);
        if (!lifetime->valid || !lifetime->dao) return;
        ++lifetime->activeCalls;
        lifetime_ = lifetime;
        dao_ = lifetime->dao;
    }

    ~DaoCallLease()
    {
        if (!lifetime_) return;
        std::lock_guard<std::mutex> lock(lifetime_->mutex);
        --lifetime_->activeCalls;
        if (lifetime_->activeCalls == 0) lifetime_->drained.notify_all();
    }

    ArtworkInteractionDAO* get() const { return dao_; }

private:
    DaoCallLease(const DaoCallLease&);
    DaoCallLease& operator=(const DaoCallLease&);

    std::shared_ptr<Lifetime> lifetime_;
    ArtworkInteractionDAO* dao_;
};

ArtworkInteractionService::ArtworkInteractionService(const ExhibitionCatalog* catalog,
                                                     SessionService* sessions,
                                                     ArtworkInteractionDAO* dao)
    : catalog_(catalog), sessions_(sessions), lifetime_(new Lifetime(dao))
{
}

ArtworkInteractionService::~ArtworkInteractionService()
{
    std::shared_ptr<Lifetime> lifetime = lifetime_;
    if (lifetime)
    {
        std::unique_lock<std::mutex> lock(lifetime->mutex);
        lifetime->valid = false;
        while (lifetime->activeCalls != 0) lifetime->drained.wait(lock);
        lifetime->dao = 0;
    }
    lifetime_.reset();
}

ArtworkInteractionResult ArtworkInteractionService::result(
    ArtworkInteractionResult::Status status)
{
    ArtworkInteractionResult value;
    value.status = status;
    return value;
}

bool ArtworkInteractionService::active(const std::weak_ptr<Lifetime>& weakLifetime)
{
    const std::shared_ptr<Lifetime> lifetime = weakLifetime.lock();
    if (!lifetime) return false;
    std::lock_guard<std::mutex> lock(lifetime->mutex);
    return lifetime->valid;
}

const ArtworkInfo* ArtworkInteractionService::findArtwork(const std::string& artworkId) const
{
    return catalog_ ? catalog_->findArtwork(artworkId) : 0;
}

bool ArtworkInteractionService::artworkExists(const std::string& artworkId,
                                              const Callback& callback) const
{
    if (findArtwork(artworkId)) return true;
    if (callback) callback(result(ArtworkInteractionResult::kNotFound));
    return false;
}

void ArtworkInteractionService::authenticate(const std::string& token, const Callback& failure,
                                             const AuthenticatedCallback& success) const
{
    if (token.empty())
    {
        if (failure) failure(result(ArtworkInteractionResult::kUnauthorized));
        return;
    }
    if (!sessions_)
    {
        if (failure) failure(result(ArtworkInteractionResult::kUnavailable));
        return;
    }
    const std::weak_ptr<Lifetime> lifetime(lifetime_);
    sessions_->get(token, [failure, success, lifetime](const std::shared_ptr<Session>& session) {
        if (!active(lifetime))
        {
            if (failure) failure(result(ArtworkInteractionResult::kUnavailable));
            return;
        }
        if (!session || session->status != 1)
        {
            if (failure) failure(result(ArtworkInteractionResult::kUnauthorized));
            return;
        }
        if (success) success(session->userId);
    });
}

void ArtworkInteractionService::loadSummary(const std::string& artworkId, uint64_t userId,
                                            const Callback& callback) const
{
    const std::weak_ptr<Lifetime> lifetime(lifetime_);
    DaoCallLease lease(lifetime);
    ArtworkInteractionDAO* dao = lease.get();
    if (!dao)
    {
        if (callback) callback(result(ArtworkInteractionResult::kUnavailable));
        return;
    }
    dao->summary(artworkId, userId,
                 [callback, lifetime](bool ok, uint64_t count, bool liked,
                                      uint64_t commentCount) {
        if (!callback) return;
        if (!active(lifetime))
        {
            callback(result(ArtworkInteractionResult::kUnavailable));
            return;
        }
        if (!ok)
        {
            callback(result(ArtworkInteractionResult::kUnavailable));
            return;
        }
        ArtworkInteractionResult value;
        value.likeCount = count;
        value.commentCount = commentCount;
        value.liked = liked;
        callback(value);
    });
}

void ArtworkInteractionService::detail(const std::string& token, const std::string& artworkId,
                                       const Callback& callback) const
{
    if (!artworkExists(artworkId, callback)) return;
    if (token.empty())
    {
        loadSummary(artworkId, 0, callback);
        return;
    }
    const std::weak_ptr<Lifetime> lifetime(lifetime_);
    authenticate(token, callback, [artworkId, callback, lifetime](uint64_t userId) {
        DaoCallLease lease(lifetime);
        ArtworkInteractionDAO* dao = lease.get();
        if (!dao)
        {
            if (callback) callback(result(ArtworkInteractionResult::kUnavailable));
            return;
        }
        dao->summary(artworkId, userId,
                     [callback, lifetime](bool ok, uint64_t count, bool liked,
                                          uint64_t commentCount) {
            if (!callback) return;
            if (!active(lifetime) || !ok)
            {
                callback(result(ArtworkInteractionResult::kUnavailable));
                return;
            }
            ArtworkInteractionResult value;
            value.likeCount = count;
            value.commentCount = commentCount;
            value.liked = liked;
            callback(value);
        });
    });
}

void ArtworkInteractionService::like(const std::string& token, const std::string& artworkId,
                                     const Callback& callback) const
{
    if (!artworkExists(artworkId, callback)) return;
    const std::weak_ptr<Lifetime> lifetime(lifetime_);
    authenticate(token, callback, [artworkId, callback, lifetime](uint64_t userId) {
        DaoCallLease lease(lifetime);
        ArtworkInteractionDAO* dao = lease.get();
        if (!dao)
        {
            if (callback) callback(result(ArtworkInteractionResult::kUnavailable));
            return;
        }
        dao->like(artworkId, userId, [callback, lifetime](bool ok, bool, uint64_t count,
                                                          bool liked) {
            if (!callback) return;
            if (!active(lifetime) || !ok)
            {
                callback(result(ArtworkInteractionResult::kUnavailable));
                return;
            }
            ArtworkInteractionResult value;
            value.likeCount = count;
            value.liked = liked;
            callback(value);
        });
    });
}

void ArtworkInteractionService::unlike(const std::string& token, const std::string& artworkId,
                                       const Callback& callback) const
{
    if (!artworkExists(artworkId, callback)) return;
    const std::weak_ptr<Lifetime> lifetime(lifetime_);
    authenticate(token, callback, [artworkId, callback, lifetime](uint64_t userId) {
        DaoCallLease lease(lifetime);
        ArtworkInteractionDAO* dao = lease.get();
        if (!dao)
        {
            if (callback) callback(result(ArtworkInteractionResult::kUnavailable));
            return;
        }
        dao->unlike(artworkId, userId, [callback, lifetime](bool ok, bool, uint64_t count,
                                                            bool liked) {
            if (!callback) return;
            if (!active(lifetime) || !ok)
            {
                callback(result(ArtworkInteractionResult::kUnavailable));
                return;
            }
            ArtworkInteractionResult value;
            value.likeCount = count;
            value.liked = liked;
            callback(value);
        });
    });
}

void ArtworkInteractionService::listComments(const std::string& artworkId, uint64_t beforeId,
                                             uint32_t limit, const Callback& callback) const
{
    if (!artworkExists(artworkId, callback)) return;
    const std::weak_ptr<Lifetime> lifetime(lifetime_);
    DaoCallLease lease(lifetime);
    ArtworkInteractionDAO* dao = lease.get();
    if (!dao)
    {
        if (callback) callback(result(ArtworkInteractionResult::kUnavailable));
        return;
    }
    const uint32_t boundedLimit = limit == 0 || limit > 20 ? 20 : limit;
    dao->listComments(
        artworkId, beforeId, boundedLimit,
        [callback, lifetime](bool ok, const std::vector<ArtworkComment>& comments,
                             uint64_t nextBefore) {
            if (!callback) return;
            if (!active(lifetime) || !ok)
            {
                callback(result(ArtworkInteractionResult::kUnavailable));
                return;
            }
            ArtworkInteractionResult value;
            value.comments = comments;
            value.nextBefore = nextBefore;
            callback(value);
        });
}

bool ArtworkInteractionService::validNonBlankUtf8(const std::string& content)
{
    bool hasContent = false;
    size_t position = 0;
    while (position < content.size())
    {
        uint32_t codePoint = 0;
        if (!nextCodePoint(content, &position, &codePoint)) return false;
        if (!unicodeWhitespace(codePoint)) hasContent = true;
    }
    return hasContent;
}

void ArtworkInteractionService::comment(const std::string& token, const std::string& artworkId,
                                        const std::string& content,
                                        const Callback& callback) const
{
    commentRequest(token, artworkId, content, true, callback);
}

void ArtworkInteractionService::commentRequest(const std::string& token,
                                               const std::string& artworkId,
                                               const std::string& content,
                                               bool contentValid,
                                               const Callback& callback) const
{
    if (!artworkExists(artworkId, callback)) return;
    const std::weak_ptr<Lifetime> lifetime(lifetime_);
    authenticate(token, callback,
                 [artworkId, content, contentValid, callback, lifetime](uint64_t userId) {
        if (!contentValid || content.empty() || content.size() > 1000 ||
            !validNonBlankUtf8(content))
        {
            if (callback) callback(result(ArtworkInteractionResult::kBadRequest));
            return;
        }
        DaoCallLease lease(lifetime);
        ArtworkInteractionDAO* dao = lease.get();
        if (!dao)
        {
            if (callback) callback(result(ArtworkInteractionResult::kUnavailable));
            return;
        }
        dao->createComment(artworkId, userId, content,
                           [callback, lifetime](bool ok, uint64_t id) {
            if (!callback) return;
            if (!active(lifetime) || !ok || id == 0)
            {
                callback(result(ArtworkInteractionResult::kUnavailable));
                return;
            }
            ArtworkInteractionResult value;
            value.commentId = id;
            callback(value);
        });
    });
}

} // namespace ar
