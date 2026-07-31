// Redis 在线状态实现：展馆内所有匿名访客共享一个按最近心跳排序的集合。
#include <services/PresenceService.h>

#include <cache/RedisConnectionPool.h>

#ifdef HAS_REDIS
#include <hiredis/hiredis.h>
#endif

#include <cerrno>
#include <cstdlib>

namespace ar {
namespace {

const char* const kPresenceKey = "presence:exhibition";
const int64_t kOnlineWindowMs = 60000;

} // namespace

bool PresenceService::heartbeat(const std::string& token, int64_t nowMs)
{
    return store_ && !token.empty() && nowMs >= 0 && store_->touch(token, nowMs);
}

bool PresenceService::remove(const std::string& token)
{
    return store_ && !token.empty() && store_->remove(token);
}

bool PresenceService::count(int64_t nowMs, uint64_t* value)
{
    if (!value) return false;
    *value = 0;
    return store_ && nowMs >= 0 && store_->count(nowMs - kOnlineWindowMs, value);
}

bool PresenceService::heartbeat(const std::string& sceneId, const std::string& token, int64_t nowMs)
{
    return !sceneId.empty() && heartbeat(token, nowMs);
}

bool PresenceService::remove(const std::string& sceneId, const std::string& token)
{
    return !sceneId.empty() && remove(token);
}

bool PresenceService::list(const std::string& sceneId, int64_t nowMs,
                           std::vector<PresenceEntry>* entries)
{
    if (!entries) return false;
    entries->clear();
    return store_ && !sceneId.empty() &&
           store_->active(sceneId, nowMs - kOnlineWindowMs, entries);
}

bool RedisPresenceStore::touch(const std::string& token, int64_t nowMs)
{
#ifdef HAS_REDIS
    if (!pool_ || token.empty()) return false;
    std::shared_ptr<redisContext> connection = pool_->borrow();
    if (!connection) return false;
    redisReply* reply = static_cast<redisReply*>(redisCommand(connection.get(), "ZADD %s %lld %s",
        kPresenceKey, static_cast<long long>(nowMs), token.c_str()));
    if (!reply) return false;
    const bool ok = reply->type == REDIS_REPLY_INTEGER;
    freeReplyObject(reply);
    return ok;
#else
    (void)token; (void)nowMs;
    return false;
#endif
}

bool RedisPresenceStore::remove(const std::string& token)
{
#ifdef HAS_REDIS
    if (!pool_ || token.empty()) return false;
    std::shared_ptr<redisContext> connection = pool_->borrow();
    if (!connection) return false;
    redisReply* reply = static_cast<redisReply*>(redisCommand(connection.get(), "ZREM %s %s",
        kPresenceKey, token.c_str()));
    if (!reply) return false;
    const bool ok = reply->type == REDIS_REPLY_INTEGER;
    freeReplyObject(reply);
    return ok;
#else
    (void)token;
    return false;
#endif
}

bool RedisPresenceStore::count(int64_t cutoffMs, uint64_t* value)
{
#ifdef HAS_REDIS
    if (!pool_ || !value) return false;
    *value = 0;
    std::shared_ptr<redisContext> connection = pool_->borrow();
    if (!connection) return false;
    static const char script[] =
        "redis.call('ZREMRANGEBYSCORE',KEYS[1],'-inf',ARGV[1]);"
        "return redis.call('ZCARD',KEYS[1])";
    redisReply* reply = static_cast<redisReply*>(redisCommand(
        connection.get(), "EVAL %s 1 %s %lld", script, kPresenceKey,
        static_cast<long long>(cutoffMs)));
    if (!reply) return false;
    const bool ok = reply->type == REDIS_REPLY_INTEGER && reply->integer >= 0;
    if (ok) *value = static_cast<uint64_t>(reply->integer);
    freeReplyObject(reply);
    return ok;
#else
    (void)cutoffMs; (void)value;
    return false;
#endif
}

bool RedisPresenceStore::touch(const std::string& sceneId, const std::string& token, int64_t nowMs)
{
    return !sceneId.empty() && touch(token, nowMs);
}

bool RedisPresenceStore::remove(const std::string& sceneId, const std::string& token)
{
    return !sceneId.empty() && remove(token);
}

bool RedisPresenceStore::active(const std::string& sceneId, int64_t cutoffMs,
                                std::vector<PresenceEntry>* entries)
{
#ifdef HAS_REDIS
    if (!pool_ || !entries || sceneId.empty()) return false;
    entries->clear();
    std::shared_ptr<redisContext> connection = pool_->borrow();
    if (!connection) return false;
    redisReply* trim = static_cast<redisReply*>(redisCommand(connection.get(),
        "ZREMRANGEBYSCORE %s -inf %lld", kPresenceKey, static_cast<long long>(cutoffMs)));
    if (!trim) return false;
    const bool trimmed = trim->type == REDIS_REPLY_INTEGER;
    freeReplyObject(trim);
    if (!trimmed) return false;
    redisReply* reply = static_cast<redisReply*>(redisCommand(connection.get(),
        "ZRANGEBYSCORE %s %lld +inf WITHSCORES", kPresenceKey, static_cast<long long>(cutoffMs)));
    if (!reply) return false;
    const bool valid = reply->type == REDIS_REPLY_ARRAY && reply->elements % 2 == 0;
    if (valid)
    {
        for (size_t index = 0; index < reply->elements; index += 2)
        {
            redisReply* token = reply->element[index];
            redisReply* score = reply->element[index + 1];
            if (!token || !score || token->type != REDIS_REPLY_STRING || score->type != REDIS_REPLY_STRING)
            { freeReplyObject(reply); return false; }
            errno = 0;
            char* end = 0;
            const long long seen = std::strtoll(score->str, &end, 10);
            if (errno == ERANGE || !end || *end != '\0') { freeReplyObject(reply); return false; }
            entries->push_back(PresenceEntry(std::string(token->str, token->len), seen));
        }
    }
    freeReplyObject(reply);
    return valid;
#else
    (void)sceneId; (void)cutoffMs; (void)entries;
    return false;
#endif
}

} // namespace ar
