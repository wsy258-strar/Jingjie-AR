#pragma once

#include <http/HttpRequest.h>
#include <session/SessionStorage.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace http {
namespace session {

class SessionValidator
{
public:
    virtual ~SessionValidator() {}

    virtual void validate(const std::string& token,
                          const std::function<void(bool)>& completion) = 0;
};

class SessionManager : public SessionValidator
{
public:
    SessionManager(std::unique_ptr<SessionStorage> storage, int64_t ttlSeconds);

    Session create();
    bool save(const Session& session);
    bool load(const std::string& id, Session* session);
    bool refresh(const std::string& id, Session* session);
    bool destroy(const std::string& id);

    void validate(const std::string& token,
                  const std::function<void(bool)>& completion) override;

    static std::string extractToken(const HttpRequest& request);

private:
    static int64_t nowMilliseconds();
    static std::string secureId();

    std::unique_ptr<SessionStorage> storage_;
    int64_t ttlSeconds_;
};

} // namespace session
} // namespace http
