// 匿名访客会话服务：负责安全 Token、初始化幂等和 Redis 会话有效期。
#pragma once

#include <functional>
#include <string>

class RedisConnectionPool;

namespace ar {

class VisitorStore
{
public:
    virtual ~VisitorStore() {}
    virtual bool exists(const std::string& token) = 0;
    virtual bool save(const std::string& token, int ttlSeconds) = 0;
    virtual bool claimBootstrap(const std::string& requestId,
                                const std::string& candidateToken,
                                int ttlSeconds,
                                std::string* resolvedToken,
                                bool* claimed) = 0;
};

struct VisitorBootstrapResult
{
    enum Status
    {
        kOk,
        kBadRequest,
        kUnavailable
    };

    VisitorBootstrapResult() : status(kUnavailable), incrementView(false) {}

    Status status;
    std::string token;
    bool incrementView;
};

class VisitorSessionService
{
public:
    typedef std::function<std::string()> TokenGenerator;

    explicit VisitorSessionService(VisitorStore* store,
                                   const TokenGenerator& generator = TokenGenerator());

    VisitorBootstrapResult bootstrap(const std::string& existingToken,
                                     const std::string& bootstrapRequestId);
    bool valid(const std::string& token) const;

private:
    static std::string secureToken();
    static bool validTokenSyntax(const std::string& token);
    static bool validRequestId(const std::string& requestId);

    VisitorStore* store_;
    TokenGenerator generator_;
};

class RedisVisitorStore : public VisitorStore
{
public:
    explicit RedisVisitorStore(RedisConnectionPool* pool) : pool_(pool) {}

    bool exists(const std::string& token) override;
    bool save(const std::string& token, int ttlSeconds) override;
    bool claimBootstrap(const std::string& requestId,
                        const std::string& candidateToken,
                        int ttlSeconds,
                        std::string* resolvedToken,
                        bool* claimed) override;

private:
    static std::string visitorKey(const std::string& token);
    static std::string bootstrapKey(const std::string& requestId);

    RedisConnectionPool* pool_;
};

} // namespace ar
