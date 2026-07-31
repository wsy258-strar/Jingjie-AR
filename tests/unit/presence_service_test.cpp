#include "TestSupport.h"

#include <services/PresenceService.h>

#include <map>

class FakePresenceStore : public ar::PresenceStore
{
public:
    bool touch(const std::string& token, int64_t time) override
    { entries[token] = time; return true; }
    bool remove(const std::string& token) override
    { entries.erase(token); return true; }
    bool count(int64_t cutoff, uint64_t* output) override
    {
        if (!output) return false;
        for (std::map<std::string, int64_t>::iterator it = entries.begin(); it != entries.end(); )
        {
            if (it->second <= cutoff) entries.erase(it++);
            else ++it;
        }
        *output = entries.size();
        return true;
    }
    std::map<std::string, int64_t> entries;
};

int main()
{
    FakePresenceStore store;
    ar::PresenceService service(&store);
    CHECK(service.heartbeat("token-a", 100000));
    CHECK(service.heartbeat("token-b", 100001));
    uint64_t online = 0;
    CHECK(service.count(100001, &online));
    CHECK(online == 2);

    // 场景切换只会刷新同一访客的心跳，不会产生新的展馆在线成员。
    CHECK(service.heartbeat("token-a", 110000));
    CHECK(service.count(110000, &online));
    CHECK(online == 2);

    CHECK(service.count(160002, &online));
    CHECK(online == 1);
    CHECK(service.count(170001, &online));
    CHECK(online == 0);

    CHECK(service.heartbeat("token-c", 180000));
    CHECK(service.remove("token-c"));
    CHECK(service.count(180000, &online));
    CHECK(online == 0);

    CHECK(!service.heartbeat("", 180000));
    CHECK(!service.remove(""));
    CHECK(!service.count(180000, 0));
    return 0;
}
