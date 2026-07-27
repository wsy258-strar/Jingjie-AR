// 在线状态服务：以 Redis 心跳 TTL 为准维护成员在线性，不将匿名浏览计入协作人数。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class RedisConnectionPool;

namespace ar {

struct PresenceEntry
{
    PresenceEntry() : lastSeenMs(0) {}
    PresenceEntry(const std::string& value, int64_t seen) : token(value), lastSeenMs(seen) {}
    std::string token;
    int64_t lastSeenMs;
};

class PresenceStore
{
public:
    virtual ~PresenceStore() {}
    virtual bool touch(const std::string& sceneId, const std::string& token, int64_t nowMs) = 0;
    virtual bool remove(const std::string& sceneId, const std::string& token) = 0;
    virtual bool active(const std::string& sceneId, int64_t cutoffMs,
                        std::vector<PresenceEntry>* entries) = 0;
};

class PresenceService
{
public:
    explicit PresenceService(PresenceStore* store) : store_(store) {}
    bool heartbeat(const std::string& sceneId, const std::string& token, int64_t nowMs);
    bool remove(const std::string& sceneId, const std::string& token);
    bool list(const std::string& sceneId, int64_t nowMs, std::vector<PresenceEntry>* entries);
private:
    PresenceStore* store_;
};

class RedisPresenceStore : public PresenceStore
{
public:
    explicit RedisPresenceStore(RedisConnectionPool* pool) : pool_(pool) {}
    bool touch(const std::string& sceneId, const std::string& token, int64_t nowMs) override;
    bool remove(const std::string& sceneId, const std::string& token) override;
    bool active(const std::string& sceneId, int64_t cutoffMs,
                std::vector<PresenceEntry>* entries) override;
private:
    static std::string key(const std::string& sceneId);
    RedisConnectionPool* pool_;
};

} // namespace ar
