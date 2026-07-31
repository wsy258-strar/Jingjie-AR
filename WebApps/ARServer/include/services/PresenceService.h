// 在线状态服务：以展馆级 Redis 有序集合维护匿名访客在线人数。
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
    virtual bool touch(const std::string& token, int64_t nowMs) = 0;
    virtual bool remove(const std::string& token) = 0;
    virtual bool count(int64_t cutoffMs, uint64_t* value) = 0;

    // 迁移期兼容旧端点；Task 6 切换路由后删除。
    virtual bool touch(const std::string& sceneId, const std::string& token, int64_t nowMs)
    { (void)sceneId; return touch(token, nowMs); }
    virtual bool remove(const std::string& sceneId, const std::string& token)
    { (void)sceneId; return remove(token); }
    virtual bool active(const std::string& sceneId, int64_t cutoffMs,
                        std::vector<PresenceEntry>* entries)
    { (void)sceneId; (void)cutoffMs; (void)entries; return false; }
};

class PresenceService
{
public:
    explicit PresenceService(PresenceStore* store) : store_(store) {}
    bool heartbeat(const std::string& token, int64_t nowMs);
    bool remove(const std::string& token);
    bool count(int64_t nowMs, uint64_t* value);

    // 迁移期兼容旧端点；所有操作仍落在同一个展馆集合。
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
    bool touch(const std::string& token, int64_t nowMs) override;
    bool remove(const std::string& token) override;
    bool count(int64_t cutoffMs, uint64_t* value) override;

    bool touch(const std::string& sceneId, const std::string& token, int64_t nowMs) override;
    bool remove(const std::string& sceneId, const std::string& token) override;
    bool active(const std::string& sceneId, int64_t cutoffMs,
                std::vector<PresenceEntry>* entries) override;
private:
    RedisConnectionPool* pool_;
};

} // namespace ar
