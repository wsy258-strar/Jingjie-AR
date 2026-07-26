#include <session/SessionStorage.h>

#include <chrono>

namespace http {
namespace session {

int64_t MemorySessionStorage::nowMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool MemorySessionStorage::save(const Session& session)
{
    if (session.id().empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[session.id()] = session;
    return true;
}

bool MemorySessionStorage::load(const std::string& id, Session* session)
{
    if (session == 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, Session>::iterator found = sessions_.find(id);
    if (found == sessions_.end())
    {
        return false;
    }
    if (found->second.expired(nowMilliseconds()))
    {
        sessions_.erase(found);
        return false;
    }

    *session = found->second;
    return true;
}

bool MemorySessionStorage::remove(const std::string& id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.erase(id) != 0;
}

} // namespace session
} // namespace http
