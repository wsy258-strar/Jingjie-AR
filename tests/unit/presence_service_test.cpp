#include "TestSupport.h"

#include <services/PresenceService.h>

#include <map>

class FakePresenceStore : public ar::PresenceStore
{
public:
    bool touch(const std::string& scene, const std::string& token, int64_t time) override
    { entries[scene][token] = time; return true; }
    bool remove(const std::string& scene, const std::string& token) override
    { entries[scene].erase(token); return true; }
    bool active(const std::string& scene, int64_t cutoff, std::vector<ar::PresenceEntry>* output) override
    { for (std::map<std::string, int64_t>::const_iterator it = entries[scene].begin(); it != entries[scene].end(); ++it) if (it->second >= cutoff) output->push_back(ar::PresenceEntry(it->first, it->second)); return true; }
    std::map<std::string, std::map<std::string, int64_t> > entries;
};

int main()
{
    FakePresenceStore store;
    ar::PresenceService service(&store);
    CHECK(service.heartbeat("scene-1", "token-a", 100000));
    std::vector<ar::PresenceEntry> entries;
    CHECK(service.list("scene-1", 130000, &entries));
    CHECK(entries.empty());
    CHECK(service.heartbeat("scene-1", "token-a", 130001));
    CHECK(service.list("scene-1", 130000, &entries));
    CHECK(entries.size() == 1);
    CHECK(entries[0].lastSeenMs == 130001);
    CHECK(service.remove("scene-1", "token-a"));
    return 0;
}
