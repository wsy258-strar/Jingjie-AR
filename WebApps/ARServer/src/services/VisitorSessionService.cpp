// 匿名访客会话实现：Redis 认领键只绑定同页面重试身份，浏览幂等由 MySQL 保证。
#include <services/VisitorSessionService.h>

#include <cache/RedisConnectionPool.h>

#ifdef HAS_REDIS
#include <hiredis/hiredis.h>
#endif

#include <cerrno>
#include <cctype>
#include <memory>

#ifdef __linux__
#include <sys/random.h>
#endif

namespace ar {
namespace {

const int kVisitorTtlSeconds = 1800;
const int kBootstrapTtlSeconds = 300;

} // namespace

VisitorSessionService::VisitorSessionService(VisitorStore* store,
                                             const TokenGenerator& generator)
    : store_(store), generator_(generator ? generator : &VisitorSessionService::secureToken)
{
}

bool VisitorSessionService::validTokenSyntax(const std::string& token)
{
    if (token.size() != 64) return false;
    for (size_t index = 0; index < token.size(); ++index)
    {
        const char value = token[index];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) return false;
    }
    return true;
}

bool VisitorSessionService::validRequestId(const std::string& requestId)
{
    if (requestId.empty() || requestId.size() > 128) return false;
    for (size_t index = 0; index < requestId.size(); ++index)
    {
        const unsigned char value = static_cast<unsigned char>(requestId[index]);
        if (!(std::isalnum(value) || value == '-' || value == '_' || value == '.' || value == ':'))
            return false;
    }
    return true;
}

std::string VisitorSessionService::secureToken()
{
#ifdef __linux__
    unsigned char bytes[32];
    size_t offset = 0;
    while (offset < sizeof(bytes))
    {
        const ssize_t read = getrandom(bytes + offset, sizeof(bytes) - offset, 0);
        if (read < 0)
        {
            if (errno == EINTR) continue;
            return std::string();
        }
        if (read == 0) return std::string();
        offset += static_cast<size_t>(read);
    }

    static const char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(sizeof(bytes) * 2);
    for (size_t index = 0; index < sizeof(bytes); ++index)
    {
        token.push_back(hex[(bytes[index] >> 4) & 0x0f]);
        token.push_back(hex[bytes[index] & 0x0f]);
    }
    return token;
#else
    return std::string();
#endif
}

bool VisitorSessionService::valid(const std::string& token) const
{
    return store_ && validTokenSyntax(token) && store_->exists(token);
}

bool VisitorSessionService::refresh(const std::string& token)
{
    return store_ && validTokenSyntax(token) && store_->save(token, kVisitorTtlSeconds);
}

VisitorBootstrapResult VisitorSessionService::bootstrap(
    const std::string& existingToken,
    const std::string& bootstrapRequestId)
{
    VisitorBootstrapResult result;
    if (!validRequestId(bootstrapRequestId))
    {
        result.status = VisitorBootstrapResult::kBadRequest;
        return result;
    }
    if (!store_)
    {
        result.status = VisitorBootstrapResult::kUnavailable;
        return result;
    }

    std::string candidateToken;
    if (valid(existingToken))
        candidateToken = existingToken;
    else
        candidateToken = generator_();

    if (!validTokenSyntax(candidateToken) ||
        !store_->save(candidateToken, kVisitorTtlSeconds))
    {
        result.status = VisitorBootstrapResult::kUnavailable;
        return result;
    }

    std::string resolvedToken;
    if (!store_->claimBootstrap(bootstrapRequestId, candidateToken,
                                kBootstrapTtlSeconds, &resolvedToken) ||
        !validTokenSyntax(resolvedToken) ||
        !store_->save(resolvedToken, kVisitorTtlSeconds))
    {
        result.status = VisitorBootstrapResult::kUnavailable;
        return result;
    }

    result.status = VisitorBootstrapResult::kOk;
    result.token = resolvedToken;
    return result;
}

std::string RedisVisitorStore::visitorKey(const std::string& token)
{
    return "visitor:{" + token + "}";
}

std::string RedisVisitorStore::bootstrapKey(const std::string& requestId)
{
    return "bootstrap:{" + requestId + "}";
}

bool RedisVisitorStore::exists(const std::string& token)
{
#ifdef HAS_REDIS
    if (!pool_ || token.empty()) return false;
    std::shared_ptr<redisContext> connection = pool_->borrow();
    if (!connection) return false;
    const std::string key = visitorKey(token);
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(connection.get(), "EXISTS %s", key.c_str()));
    if (!reply) return false;
    const bool found = reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    freeReplyObject(reply);
    return found;
#else
    (void)token;
    return false;
#endif
}

bool RedisVisitorStore::save(const std::string& token, int ttlSeconds)
{
#ifdef HAS_REDIS
    if (!pool_ || token.empty() || ttlSeconds <= 0) return false;
    std::shared_ptr<redisContext> connection = pool_->borrow();
    if (!connection) return false;
    const std::string key = visitorKey(token);
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(connection.get(), "SET %s 1 EX %d", key.c_str(), ttlSeconds));
    if (!reply) return false;
    const bool saved = reply->type == REDIS_REPLY_STATUS &&
                       reply->str && std::string(reply->str, reply->len) == "OK";
    freeReplyObject(reply);
    return saved;
#else
    (void)token;
    (void)ttlSeconds;
    return false;
#endif
}

bool RedisVisitorStore::claimBootstrap(const std::string& requestId,
                                       const std::string& candidateToken,
                                       int ttlSeconds,
                                       std::string* resolvedToken)
{
#ifdef HAS_REDIS
    if (!pool_ || requestId.empty() || candidateToken.empty() || ttlSeconds <= 0 ||
        !resolvedToken)
        return false;
    resolvedToken->clear();

    std::shared_ptr<redisContext> connection = pool_->borrow();
    if (!connection) return false;
    const std::string key = bootstrapKey(requestId);
    redisReply* setReply = static_cast<redisReply*>(
        redisCommand(connection.get(), "SET %s %s NX EX %d",
                     key.c_str(), candidateToken.c_str(), ttlSeconds));
    if (!setReply) return false;
    if (setReply->type == REDIS_REPLY_STATUS)
    {
        if (!setReply->str || std::string(setReply->str, setReply->len) != "OK")
        {
            freeReplyObject(setReply);
            return false;
        }
    }
    else if (setReply->type != REDIS_REPLY_NIL)
    {
        freeReplyObject(setReply);
        return false;
    }
    freeReplyObject(setReply);

    redisReply* getReply = static_cast<redisReply*>(
        redisCommand(connection.get(), "GET %s", key.c_str()));
    if (!getReply) return false;
    if (getReply->type != REDIS_REPLY_STRING || !getReply->str)
    {
        freeReplyObject(getReply);
        return false;
    }
    resolvedToken->assign(getReply->str, getReply->len);
    freeReplyObject(getReply);
    return true;
#else
    (void)requestId;
    (void)candidateToken;
    (void)ttlSeconds;
    (void)resolvedToken;
    return false;
#endif
}

} // namespace ar
