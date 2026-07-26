#pragma once

#include <session/SessionStorage.h>

class RedisConnectionPool;

namespace http {
namespace session {

class RedisSessionStorage : public SessionStorage
{
public:
    explicit RedisSessionStorage(RedisConnectionPool* pool);

    bool save(const Session& session) override;
    bool load(const std::string& id, Session* session) override;
    bool remove(const std::string& id) override;

    static std::string encode(const Session& session);
    static bool decode(const std::string& encoded, Session* session);

private:
    static std::string key(const std::string& id);

    RedisConnectionPool* pool_;
};

} // namespace session
} // namespace http
