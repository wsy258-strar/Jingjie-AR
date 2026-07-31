// 认证服务实现：密码哈希校验完成后再创建会话，避免凭据逻辑散落在处理器中。
#include <services/AuthService.h>

#include <utils/JsonUtil.h>

#include <argon2.h>

#include <sstream>
#include <iomanip>
#include <cerrno>
#include <vector>

#ifdef __linux__
#include <sys/random.h>
#endif

#include <openssl/sha.h>
#include <openssl/crypto.h>

namespace {

const uint32_t kArgon2MemoryKiB = 64U * 1024U;
const uint32_t kArgon2Iterations = 3U;
const uint32_t kArgon2Parallelism = 1U;
const size_t kArgon2SaltBytes = 16U;
const size_t kArgon2HashBytes = 32U;

bool secureRandom(unsigned char* bytes, size_t length)
{
#ifdef __linux__
    size_t offset = 0;
    while (offset < length)
    {
        const ssize_t read = getrandom(bytes + offset, length - offset, 0);
        if (read < 0)
        {
            if (errno == EINTR) continue;
            return false;
        }
        if (read == 0) return false;
        offset += static_cast<size_t>(read);
    }
    return true;
#else
    (void)bytes;
    (void)length;
    return false;
#endif
}

} // namespace

namespace ar {

std::string AuthService::json(const AuthResult& result)
{
    std::ostringstream output;
    output << "{\"isNew\":"
           << (result.isNew ? "true" : "false")
           << ",\"username\":\"" << JsonUtil::escape(result.username)
           << "\",\"userId\":" << result.userId
           << ",\"token\":\"" << JsonUtil::escape(result.sessionToken)
           << "\"}";
    return output.str();
}

std::string AuthService::token()
{
    unsigned char bytes[32];
    if (!secureRandom(bytes, sizeof(bytes))) return std::string();
    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(sizeof(bytes) * 2);
    for (size_t index = 0; index < sizeof(bytes); ++index)
    {
        result.push_back(hex[(bytes[index] >> 4) & 0x0f]);
        result.push_back(hex[bytes[index] & 0x0f]);
    }
    return result;
}

std::string AuthService::passwordHash(const std::string& password)
{
    unsigned char salt[kArgon2SaltBytes];
    unsigned char rawHash[kArgon2HashBytes];
    if (!secureRandom(salt, sizeof(salt))) return std::string();
    const size_t encodedLength = argon2_encodedlen(
        kArgon2Iterations, kArgon2MemoryKiB, kArgon2Parallelism,
        kArgon2SaltBytes, kArgon2HashBytes, Argon2_id);
    std::vector<char> encoded(encodedLength, '\0');
    const int status = argon2_hash(
        kArgon2Iterations, kArgon2MemoryKiB, kArgon2Parallelism,
        password.data(), password.size(), salt, sizeof(salt),
        rawHash, sizeof(rawHash), &encoded[0], encoded.size(),
        Argon2_id, ARGON2_VERSION_13);
    OPENSSL_cleanse(salt, sizeof(salt));
    OPENSSL_cleanse(rawHash, sizeof(rawHash));
    if (status != ARGON2_OK) return std::string();
    return std::string(&encoded[0]);
}

std::string AuthService::legacyPasswordHash(const std::string& password)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(password.data()), password.size(), digest);
    std::ostringstream output;
    output << "sha256:";
    for (size_t index = 0; index < sizeof(digest); ++index)
        output << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(digest[index]);
    return output.str();
}

bool AuthService::verifyPassword(const std::string& password,
                                 const std::string& storedHash,
                                 bool* needsUpgrade)
{
    if (needsUpgrade) *needsUpgrade = false;
    if (storedHash.compare(0, 10, "$argon2id$") == 0)
        return argon2id_verify(storedHash.c_str(), password.data(), password.size()) == ARGON2_OK;
    if (storedHash.compare(0, 7, "sha256:") != 0) return false;

    const std::string candidate = legacyPasswordHash(password);
    const bool matches = candidate.size() == storedHash.size() &&
        CRYPTO_memcmp(candidate.data(), storedHash.data(), storedHash.size()) == 0;
    if (matches && needsUpgrade) *needsUpgrade = true;
    return matches;
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
    store_->findUser(username, [this, username, password, completion](const std::shared_ptr<User>& user) {
        if (user) {
            bool needsUpgrade = false;
            if (!verifyPassword(password, user->passwdHash, &needsUpgrade))
            {
                AuthResult result;
                completion(result, 401);
                return;
            }
            if (needsUpgrade)
            {
                const std::string upgradedHash = passwordHash(password);
                if (upgradedHash.empty())
                {
                    AuthResult result;
                    completion(result, 503);
                    return;
                }
                const uint64_t userId = user->id;
                const std::string storedUsername = user->username;
                store_->updatePasswordHash(
                    userId, upgradedHash,
                    [this, userId, storedUsername, completion](bool updated) {
                        if (!updated)
                        {
                            AuthResult result;
                            completion(result, 503);
                            return;
                        }
                        createSession(userId, storedUsername, false, completion);
                    });
                return;
            }
            createSession(user->id, user->username, false, completion);
            return;
        }
        const std::string hashedPassword = passwordHash(password);
        if (hashedPassword.empty())
        {
            AuthResult result;
            completion(result, 503);
            return;
        }
        store_->createUser(username, hashedPassword, [this, username, completion](uint64_t userId) {
            if (userId == 0) { AuthResult result; completion(result, 503); return; }
            createSession(userId, username, true, completion);
        });
    });
}

} // namespace ar
