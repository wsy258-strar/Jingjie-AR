// 会话值对象与令牌/过期时间辅助逻辑实现。
#include <session/Session.h>

namespace http {
namespace session {

Session::Session()
    : expiresAt_(0),
      ttlSeconds_(0)
{
}

Session::Session(const std::string& id, int64_t expiresAt, int64_t ttlSeconds)
    : id_(id),
      expiresAt_(expiresAt),
      ttlSeconds_(ttlSeconds)
{
}

const std::string& Session::id() const
{
    return id_;
}

int64_t Session::expiresAt() const
{
    return expiresAt_;
}

int64_t Session::ttlSeconds() const
{
    return ttlSeconds_;
}

void Session::setValue(const std::string& key, const std::string& value)
{
    values_[key] = value;
}

std::string Session::value(const std::string& key) const
{
    std::unordered_map<std::string, std::string>::const_iterator value = values_.find(key);
    return value == values_.end() ? std::string() : value->second;
}

bool Session::remove(const std::string& key)
{
    return values_.erase(key) != 0;
}

void Session::clear()
{
    values_.clear();
}

const std::unordered_map<std::string, std::string>& Session::values() const
{
    return values_;
}

void Session::refresh(int64_t nowMs)
{
    expiresAt_ = nowMs + ttlSeconds_ * 1000;
}

bool Session::expired(int64_t nowMs) const
{
    return expiresAt_ <= nowMs;
}

} // namespace session
} // namespace http
