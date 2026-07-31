#include "TestSupport.h"

#include <services/VisitorSessionService.h>

#include <atomic>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace {

const char* const kTokenA =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const char* const kTokenB =
    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";

class FakeVisitorStore : public ar::VisitorStore
{
public:
    FakeVisitorStore() : saveAvailable(true), claimAvailable(true) {}

    bool exists(const std::string& token) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return visitors.find(token) != visitors.end();
    }

    bool save(const std::string& token, int ttlSeconds) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!saveAvailable || ttlSeconds != 1800) return false;
        visitors[token] = ttlSeconds;
        return true;
    }

    bool claimBootstrap(const std::string& requestId, const std::string& candidateToken,
                        int ttlSeconds, std::string* resolvedToken, bool* claimed) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!claimAvailable || !resolvedToken || !claimed || ttlSeconds != 300) return false;
        std::map<std::string, std::string>::const_iterator existing = bootstraps.find(requestId);
        if (existing == bootstraps.end())
        {
            bootstraps[requestId] = candidateToken;
            *resolvedToken = candidateToken;
            *claimed = true;
        }
        else
        {
            *resolvedToken = existing->second;
            *claimed = false;
        }
        return true;
    }

    std::mutex mutex;
    std::map<std::string, int> visitors;
    std::map<std::string, std::string> bootstraps;
    bool saveAvailable;
    bool claimAvailable;
};

std::string tokenA()
{
    return kTokenA;
}

std::string invalidToken()
{
    return "not-a-secure-token";
}

} // namespace

int main()
{
    FakeVisitorStore store;
    ar::VisitorSessionService service(&store, tokenA);

    ar::VisitorBootstrapResult first = service.bootstrap("", "page-first");
    CHECK(first.status == ar::VisitorBootstrapResult::kOk);
    CHECK(first.token == kTokenA);
    CHECK(first.incrementView);
    CHECK(service.valid(kTokenA));

    ar::VisitorBootstrapResult retry = service.bootstrap("", "page-first");
    CHECK(retry.status == ar::VisitorBootstrapResult::kOk);
    CHECK(retry.token == first.token);
    CHECK(!retry.incrementView);

    ar::VisitorBootstrapResult refreshed = service.bootstrap(kTokenA, "page-refresh");
    CHECK(refreshed.status == ar::VisitorBootstrapResult::kOk);
    CHECK(refreshed.token == kTokenA);
    CHECK(refreshed.incrementView);

    CHECK(service.bootstrap("", "").status == ar::VisitorBootstrapResult::kBadRequest);
    CHECK(service.bootstrap("", "contains space").status == ar::VisitorBootstrapResult::kBadRequest);
    CHECK(service.bootstrap("", std::string(129, 'x')).status ==
          ar::VisitorBootstrapResult::kBadRequest);
    CHECK(!service.valid("ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));
    CHECK(!service.valid("short"));

    FakeVisitorStore badGeneratorStore;
    ar::VisitorSessionService badGenerator(&badGeneratorStore, invalidToken);
    CHECK(badGenerator.bootstrap("", "page-bad-token").status ==
          ar::VisitorBootstrapResult::kUnavailable);

    FakeVisitorStore unavailableStore;
    unavailableStore.saveAvailable = false;
    ar::VisitorSessionService saveUnavailable(&unavailableStore, tokenA);
    CHECK(saveUnavailable.bootstrap("", "page-save-failure").status ==
          ar::VisitorBootstrapResult::kUnavailable);
    unavailableStore.saveAvailable = true;
    unavailableStore.claimAvailable = false;
    CHECK(saveUnavailable.bootstrap("", "page-claim-failure").status ==
          ar::VisitorBootstrapResult::kUnavailable);

    FakeVisitorStore concurrentStore;
    std::atomic<unsigned int> sequence(0);
    ar::VisitorSessionService concurrent(
        &concurrentStore,
        [&sequence]() {
            const unsigned int value = sequence.fetch_add(1);
            std::ostringstream token;
            token << std::hex;
            for (int index = 0; index < 64; ++index)
                token << static_cast<char>("0123456789abcdef"[(value + index) % 16]);
            return token.str();
        });
    std::vector<ar::VisitorBootstrapResult> results(16);
    std::vector<std::thread> workers;
    for (size_t index = 0; index < results.size(); ++index)
    {
        workers.push_back(std::thread([&concurrent, &results, index]() {
            results[index] = concurrent.bootstrap("", "page-concurrent");
        }));
    }
    for (size_t index = 0; index < workers.size(); ++index) workers[index].join();
    size_t increments = 0;
    for (size_t index = 0; index < results.size(); ++index)
    {
        CHECK(results[index].status == ar::VisitorBootstrapResult::kOk);
        CHECK(results[index].token == results[0].token);
        if (results[index].incrementView) ++increments;
    }
    CHECK(increments == 1);
    CHECK(concurrent.valid(results[0].token));

    return 0;
}
