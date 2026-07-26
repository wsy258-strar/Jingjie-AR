#include "TestSupport.h"

#include <services/PresenceService.h>
#include <cache/RedisConnectionPool.h>

#include <cstdlib>

int main(int argc, char** argv)
{
    CHECK(argc == 2);
    RedisConnectionPool pool("127.0.0.1", std::atoi(argv[1]), 1);
    ar::RedisPresenceStore store(&pool);
    ar::PresenceService service(&store);
    CHECK(service.heartbeat("scene-redis", "token-a", 100001));
    CHECK(service.heartbeat("scene-redis", "token-b", 130001));
    std::vector<ar::PresenceEntry> entries;
    CHECK(service.list("scene-redis", 130001, &entries));
    CHECK(entries.size() == 1);
    CHECK(entries[0].token == "token-b");
    CHECK(service.remove("scene-redis", "token-b"));
    CHECK(service.list("scene-redis", 130001, &entries));
    CHECK(entries.empty());
    return 0;
}
