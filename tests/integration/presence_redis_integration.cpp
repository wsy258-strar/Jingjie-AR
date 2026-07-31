#include "TestSupport.h"

#include <services/PresenceService.h>
#include <services/VisitorSessionService.h>
#include <cache/RedisConnectionPool.h>

#include <cstdlib>

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
    CHECK(first.incrementView);
    CHECK(first.token == kTokenA);
    ar::VisitorBootstrapResult retry = visitors.bootstrap(kTokenB, "integration-bootstrap");
    CHECK(retry.status == ar::VisitorBootstrapResult::kOk);
    CHECK(!retry.incrementView);
    CHECK(retry.token == kTokenA);
    CHECK(visitors.valid(kTokenA));

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
