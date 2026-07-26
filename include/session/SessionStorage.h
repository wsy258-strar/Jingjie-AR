#pragma once

#include <session/Session.h>

#include <mutex>
#include <string>
#include <unordered_map>

namespace http {
namespace session {

class SessionStorage
{
public:
    virtual ~SessionStorage() {}

    virtual bool save(const Session& session) = 0;
    virtual bool load(const std::string& id, Session* session) = 0;
    virtual bool remove(const std::string& id) = 0;
};

class MemorySessionStorage : public SessionStorage
{
public:
    bool save(const Session& session) override;
    bool load(const std::string& id, Session* session) override;
    bool remove(const std::string& id) override;

private:
    static int64_t nowMilliseconds();

    std::mutex mutex_;
    std::unordered_map<std::string, Session> sessions_;
};

} // namespace session
} // namespace http
