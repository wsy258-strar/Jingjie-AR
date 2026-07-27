// Redis 在线状态实现：心跳覆盖 TTL，成员列表读取反映当前有效心跳集合。
#include <services/PresenceService.h>

#include <cache/RedisConnectionPool.h>

#ifdef HAS_REDIS
#include <hiredis/hiredis.h>
#endif

#include <cerrno>
#include <cstdlib>

namespace ar {

bool PresenceService::heartbeat(const std::string& sceneId, const std::string& token, int64_t nowMs)
{
    return store_ && !sceneId.empty() && !token.empty() && store_->touch(sceneId, token, nowMs);
}

bool PresenceService::remove(const std::string& sceneId, const std::string& token)
{
    return store_ && !sceneId.empty() && !token.empty() && store_->remove(sceneId, token);
}

bool PresenceService::list(const std::string& sceneId, int64_t nowMs, std::vector<PresenceEntry>* entries)
{
    if (!entries) return false;
    entries->clear();
    if (!store_ || sceneId.empty() || !store_->active(sceneId, nowMs - 30000, entries)) return false;
    const int64_t cutoff = nowMs - 30000;
    for (std::vector<PresenceEntry>::iterator it = entries->begin(); it != entries->end(); )
    {
        if (it->lastSeenMs <= cutoff) it = entries->erase(it);
        else ++it;
    }
    return true;
}

std::string RedisPresenceStore::key(const std::string& sceneId)
{
    return "scene:" + sceneId + ":presence";
}

bool RedisPresenceStore::touch(const std::string& sceneId, const std::string& token, int64_t nowMs)
{
#ifdef HAS_REDIS
    if (!pool_ || sceneId.empty() || token.empty()) return false;
    std::shared_ptr<redisContext> connection = pool_->borrow();
    if (!connection) return false;
    const std::string redisKey = key(sceneId);
    redisReply* reply = static_cast<redisReply*>(redisCommand(connection.get(), "ZADD %s %lld %s",
        redisKey.c_str(), static_cast<long long>(nowMs), token.c_str()));
    if (!reply) return false;
    const bool ok = reply->type == REDIS_REPLY_INTEGER;
    freeReplyObject(reply);
    return ok;
#else
    (void)sceneId; (void)token; (void)nowMs;
    return false;
#endif
}

bool RedisPresenceStore::remove(const std::string& sceneId, const std::string& token)
{
#ifdef HAS_REDIS
    if (!pool_ || sceneId.empty() || token.empty()) return false;
    std::shared_ptr<redisContext> connection = pool_->borrow();
    if (!connection) return false;
    const std::string redisKey = key(sceneId);
    redisReply* reply = static_cast<redisReply*>(redisCommand(connection.get(), "ZREM %s %s",
        redisKey.c_str(), token.c_str()));
    if (!reply) return false;
    const bool ok = reply->type == REDIS_REPLY_INTEGER;
    freeReplyObject(reply);
    return ok;
#else
    (void)sceneId; (void)token;
    return false;
#endif
}

bool RedisPresenceStore::active(const std::string& sceneId, int64_t cutoffMs,
                                std::vector<PresenceEntry>* entries)
{
#ifdef HAS_REDIS
    if (!pool_ || !entries || sceneId.empty()) return false;
    entries->clear();
    std::shared_ptr<redisContext> connection = pool_->borrow();
    if (!connection) return false;
    const std::string redisKey = key(sceneId);
    redisReply* trim = static_cast<redisReply*>(redisCommand(connection.get(),
        "ZREMRANGEBYSCORE %s -inf %lld", redisKey.c_str(), static_cast<long long>(cutoffMs)));
    if (!trim) return false;
    const bool trimmed = trim->type == REDIS_REPLY_INTEGER;
    freeReplyObject(trim);
    if (!trimmed) return false;
    redisReply* reply = static_cast<redisReply*>(redisCommand(connection.get(),
        "ZRANGEBYSCORE %s %lld +inf WITHSCORES", redisKey.c_str(), static_cast<long long>(cutoffMs)));
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
