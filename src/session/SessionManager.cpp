#include <session/SessionManager.h>

#include <chrono>
#include <cerrno>

#ifdef __linux__
#include <sys/random.h>
#endif

namespace http {
namespace session {

SessionManager::SessionManager(std::unique_ptr<SessionStorage> storage, int64_t ttlSeconds)
    : storage_(std::move(storage)),
      ttlSeconds_(ttlSeconds)
{
}

Session SessionManager::create()
{
    if (!storage_)
    {
        return Session();
    }

    const std::string id = secureId();
    if (id.empty())
    {
        return Session();
    }

    Session session(id, nowMilliseconds() + ttlSeconds_ * 1000, ttlSeconds_);
    return storage_->save(session) ? session : Session();
}

bool SessionManager::save(const Session& session)
{
    return storage_ && storage_->save(session);
}

bool SessionManager::load(const std::string& id, Session* session)
{
    return storage_ && storage_->load(id, session);
}

bool SessionManager::refresh(const std::string& id, Session* session)
{
    if (!storage_ || session == 0)
    {
        return false;
    }

    Session refreshed;
    if (!storage_->load(id, &refreshed))
    {
        return false;
    }
    refreshed.refresh(nowMilliseconds());
    if (!storage_->save(refreshed))
    {
        return false;
    }
    *session = refreshed;
    return true;
}

bool SessionManager::destroy(const std::string& id)
{
    return storage_ && storage_->remove(id);
}

void SessionManager::validate(const std::string& token,
                              const std::function<void(bool)>& completion)
{
    Session session;
    const bool valid = !token.empty() && load(token, &session);
    completion(valid);
}

std::string SessionManager::extractToken(const HttpRequest& request)
{
    const std::string authorization = request.getHeader("Authorization");
    const std::string bearerPrefix = "Bearer ";
    if (authorization.compare(0, bearerPrefix.size(), bearerPrefix) == 0 &&
        authorization.size() > bearerPrefix.size())
    {
        return authorization.substr(bearerPrefix.size());
    }

    const std::string sessionId = request.cookie("sessionId");
    if (!sessionId.empty())
    {
        return sessionId;
    }

    const std::map<std::string, std::string>& query = request.queryParameters();
    std::map<std::string, std::string>::const_iterator token = query.find("token");
    return token == query.end() ? std::string() : token->second;
}

int64_t SessionManager::nowMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string SessionManager::secureId()
{
#ifdef __linux__
    unsigned char bytes[32];
    size_t offset = 0;
    while (offset < sizeof(bytes))
    {
        const ssize_t read = getrandom(bytes + offset, sizeof(bytes) - offset, 0);
        if (read < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return std::string();
        }
        if (read == 0)
        {
            return std::string();
        }
        offset += static_cast<size_t>(read);
    }

    static const char hex[] = "0123456789abcdef";
    std::string id;
    id.reserve(64);
    for (size_t index = 0; index < sizeof(bytes); ++index)
    {
        id.push_back(hex[(bytes[index] >> 4) & 0x0f]);
        id.push_back(hex[bytes[index] & 0x0f]);
    }
    return id;
#else
    return std::string();
#endif
}

} // namespace session
} // namespace http
