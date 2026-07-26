#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <db/SessionDAO.h>

namespace ar {

struct AuthResult
{
    AuthResult() : userId(0), isNew(false) {}
    std::string username;
    uint64_t userId;
    std::string sessionToken;
    bool isNew;
};

class AuthStore
{
public:
    typedef std::function<void(const std::shared_ptr<User>&)> UserCallback;
    typedef std::function<void(uint64_t)> IdCallback;
    virtual ~AuthStore() {}
    virtual void findUser(const std::string& username, const UserCallback& callback) = 0;
    virtual void createUser(const std::string& username, const std::string& password,
                            const IdCallback& callback) = 0;
    virtual void createSession(uint64_t userId, const std::string& token,
                               const IdCallback& callback) = 0;
};

class AuthService
{
public:
    typedef std::function<void(const AuthResult&, int)> Completion;
    explicit AuthService(AuthStore* store) : store_(store) {}
    void authenticate(const std::string& username, const std::string& password,
                      const Completion& completion);
    static std::string json(const AuthResult& result);
    static std::string passwordHash(const std::string& password);
private:
    static std::string token();
    void createSession(uint64_t userId, const std::string& username, bool isNew,
                       const Completion& completion);
    AuthStore* store_;
};

} // namespace ar
