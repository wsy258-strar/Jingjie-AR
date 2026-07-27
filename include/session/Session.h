// HTTP 会话值对象：保存令牌、用户标识与过期时间，不负责持久化或认证决策。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace http {
namespace session {

class Session
{
public:
    Session();
    Session(const std::string& id, int64_t expiresAt, int64_t ttlSeconds);

    const std::string& id() const;
    int64_t expiresAt() const;
    int64_t ttlSeconds() const;

    void setValue(const std::string& key, const std::string& value);
    std::string value(const std::string& key) const;
    bool remove(const std::string& key);
    void clear();
    const std::unordered_map<std::string, std::string>& values() const;

    void refresh(int64_t nowMs);
    bool expired(int64_t nowMs) const;

private:
    std::string id_;
    std::unordered_map<std::string, std::string> values_;
    int64_t expiresAt_;
    int64_t ttlSeconds_;
};

} // namespace session
} // namespace http
