#include "TestSupport.h"

#include <services/PresenceService.h>
#include <services/VisitorSessionService.h>
#include <cache/RedisConnectionPool.h>

#include <cstdlib>

#include <hiredis/hiredis.h>

namespace {

const char* const kTokenA =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const char* const kTokenB =
    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";

std::string tokenA()
{
    return kTokenA;
}

} // namespace

int main(int argc, char** argv)
{
    CHECK(argc == 2);
    RedisConnectionPool pool("127.0.0.1", std::atoi(argv[1]), 1);

    ar::RedisVisitorStore visitorStore(&pool);
    ar::VisitorSessionService visitors(&visitorStore, tokenA);
    ar::VisitorBootstrapResult first = visitors.bootstrap("", "integration-bootstrap");
    CHECK(first.status == ar::VisitorBootstrapResult::kOk);
    CHECK(first.token == kTokenA);
    ar::VisitorBootstrapResult retry = visitors.bootstrap(kTokenB, "integration-bootstrap");
    CHECK(retry.status == ar::VisitorBootstrapResult::kOk);
    CHECK(retry.token == kTokenA);
    CHECK(visitors.valid(kTokenA));

    std::shared_ptr<redisContext> redis = pool.borrow();
    CHECK(redis.get() != 0);
    redisReply* ttl = static_cast<redisReply*>(
        redisCommand(redis.get(), "TTL visitor:{%s}", kTokenA));
    CHECK(ttl != 0);
    CHECK(ttl->type == REDIS_REPLY_INTEGER);
    CHECK(ttl->integer > 0 && ttl->integer <= 1800);
    freeReplyObject(ttl);

    redisReply* expire = static_cast<redisReply*>(
        redisCommand(redis.get(), "EXPIRE visitor:{%s} 1", kTokenA));
    CHECK(expire != 0 && expire->type == REDIS_REPLY_INTEGER && expire->integer == 1);
    freeReplyObject(expire);
    redis.reset();
    CHECK(visitors.refresh(kTokenA));
    redis = pool.borrow();
    CHECK(redis.get() != 0);
    ttl = static_cast<redisReply*>(redisCommand(redis.get(), "TTL visitor:{%s}", kTokenA));
    CHECK(ttl != 0 && ttl->type == REDIS_REPLY_INTEGER && ttl->integer > 1700);
    freeReplyObject(ttl);

    redisReply* removed = static_cast<redisReply*>(
        redisCommand(redis.get(), "DEL visitor:{%s}", kTokenA));
    CHECK(removed != 0 && removed->type == REDIS_REPLY_INTEGER && removed->integer == 1);
    freeReplyObject(removed);
    redis.reset();
    ar::VisitorSessionService recovery(&visitorStore, [] { return std::string(kTokenB); });
    const ar::VisitorBootstrapResult recovered =
        recovery.bootstrap("", "integration-bootstrap");
    CHECK(recovered.status == ar::VisitorBootstrapResult::kOk);
    CHECK(recovered.token == kTokenA);
    CHECK(recovery.valid(kTokenA));

    ar::RedisPresenceStore store(&pool);
    ar::PresenceService service(&store);
    CHECK(service.heartbeat(kTokenA, 100001));
    CHECK(service.heartbeat(kTokenB, 130001));
    uint64_t online = 0;
    CHECK(service.count(130001, &online));
    CHECK(online == 2);
    CHECK(service.heartbeat(kTokenB, 130002));
    CHECK(service.count(130002, &online));
    CHECK(online == 2);
    CHECK(service.count(160002, &online));
    CHECK(online == 1);
    CHECK(service.remove(kTokenB));
    CHECK(service.count(160002, &online));
    CHECK(online == 0);
    return 0;
}
