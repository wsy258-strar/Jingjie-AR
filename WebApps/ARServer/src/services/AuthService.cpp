// 认证服务实现：密码哈希校验完成后再创建会话，避免凭据逻辑散落在处理器中。
#include <services/AuthService.h>

#include <utils/JsonUtil.h>

#include <sstream>
#include <iomanip>
#include <cerrno>

#ifdef __linux__
#include <sys/random.h>
#endif

#include <openssl/sha.h>

namespace ar {

std::string AuthService::json(const AuthResult& result)
{
    std::ostringstream output;
    output << "{\"status\":\"ok\",\"is_new\":"
           << (result.isNew ? "true" : "false")
           << ",\"username\":\"" << JsonUtil::escape(result.username)
           << "\",\"user_id\":" << result.userId
           << ",\"session_token\":\"" << JsonUtil::escape(result.sessionToken)
           << "\"}";
    return output.str();
}

std::string AuthService::token()
{
#ifdef __linux__
    unsigned char bytes[32];
    size_t offset = 0;
    while (offset < sizeof(bytes))
    {
        const ssize_t read = getrandom(bytes + offset, sizeof(bytes) - offset, 0);
        if (read < 0)
        {
            if (errno == EINTR) continue;
            return std::string();
        }
        if (read == 0) return std::string();
        offset += static_cast<size_t>(read);
    }
    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(sizeof(bytes) * 2);
    for (size_t index = 0; index < sizeof(bytes); ++index)
    {
        result.push_back(hex[(bytes[index] >> 4) & 0x0f]);
        result.push_back(hex[bytes[index] & 0x0f]);
    }
    return result;
#else
    return std::string();
#endif
}

std::string AuthService::passwordHash(const std::string& password)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(password.data()), password.size(), digest);
    std::ostringstream output;
    output << "sha256:";
    for (size_t index = 0; index < sizeof(digest); ++index)
        output << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(digest[index]);
    return output.str();
}

void AuthService::createSession(uint64_t userId, const std::string& username, bool isNew,
                                const Completion& completion)
{
    const std::string sessionToken = token();
    if (sessionToken.empty())
    {
        AuthResult result;
        completion(result, 503);
        return;
    }
    store_->createSession(userId, sessionToken, [username, userId, isNew, sessionToken, completion](uint64_t id) {
        AuthResult result;
        if (id != 0) { result.username = username; result.userId = userId; result.isNew = isNew; result.sessionToken = sessionToken; completion(result, 200); }
        else completion(result, 503);
    });
}

void AuthService::authenticate(const std::string& username, const std::string& password,
                               const Completion& completion)
{
    if (!store_) { AuthResult result; completion(result, 503); return; }
    const std::string hashedPassword = passwordHash(password);
    store_->findUser(username, [this, username, hashedPassword, completion](const std::shared_ptr<User>& user) {
        if (user) {
            if (user->passwdHash != hashedPassword) { AuthResult result; completion(result, 401); return; }
            createSession(user->id, user->username, false, completion);
            return;
        }
        store_->createUser(username, hashedPassword, [this, username, completion](uint64_t userId) {
            if (userId == 0) { AuthResult result; completion(result, 503); return; }
            createSession(userId, username, true, completion);
        });
    });
}

} // namespace ar
