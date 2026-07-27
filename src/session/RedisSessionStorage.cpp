// Redis 会话读写实现：保持序列化格式与 TTL 计算集中，避免调用点各自拼装键值。
#include <session/RedisSessionStorage.h>

#include <cache/RedisConnectionPool.h>
#include <hiredis/hiredis.h>

namespace http {
namespace session {

RedisSessionStorage::RedisSessionStorage(RedisConnectionPool* pool) : pool_(pool) {}

bool RedisSessionStorage::save(const Session& session)
{
    if (!pool_ || session.ttlSeconds() <= 0) return false;
    const std::string value = encode(session);
    if (value.empty()) return false;
    std::shared_ptr<redisContext> connection = pool_->borrow();
    if (!connection) return false;
    const std::string redisKey = key(session.id());
    redisReply* reply = static_cast<redisReply*>(redisCommand(
        connection.get(), "SETEX %b %lld %b", redisKey.data(), redisKey.size(),
        static_cast<long long>(session.ttlSeconds()), value.data(), value.size()));
    if (!reply) return false;
    const bool ok = reply->type == REDIS_REPLY_STATUS;
    freeReplyObject(reply);
    return ok;
}

bool RedisSessionStorage::load(const std::string& id, Session* session)
{
    if (!pool_ || !session) return false;
    std::shared_ptr<redisContext> connection = pool_->borrow();
    if (!connection) return false;
    const std::string redisKey = key(id);
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(connection.get(), "GET %b", redisKey.data(), redisKey.size()));
    if (!reply) return false;
    const bool ok = reply->type == REDIS_REPLY_STRING &&
        decode(std::string(reply->str, reply->len), session);
    freeReplyObject(reply);
    return ok;
}

bool RedisSessionStorage::remove(const std::string& id)
{
    if (!pool_) return false;
    std::shared_ptr<redisContext> connection = pool_->borrow();
    if (!connection) return false;
    const std::string redisKey = key(id);
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(connection.get(), "DEL %b", redisKey.data(), redisKey.size()));
    if (!reply) return false;
    const bool ok = reply->type == REDIS_REPLY_INTEGER;
    freeReplyObject(reply);
    return ok;
}

} // namespace session
} // namespace http
