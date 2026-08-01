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
    FakeVisitorStore() : saveAvailable(true), claimAvailable(true), saveCalls(0) {}

    bool exists(const std::string& token) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return visitors.find(token) != visitors.end();
    }

    bool save(const std::string& token, int ttlSeconds) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!saveAvailable || ttlSeconds != 1800) return false;
        ++saveCalls;
        visitors[token] = ttlSeconds;
        return true;
    }

    bool claimBootstrap(const std::string& requestId, const std::string& candidateToken,
                        int ttlSeconds, std::string* resolvedToken) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!claimAvailable || !resolvedToken || ttlSeconds != 300) return false;
        std::map<std::string, std::string>::const_iterator existing = bootstraps.find(requestId);
        if (existing == bootstraps.end())
        {
            bootstraps[requestId] = candidateToken;
            *resolvedToken = candidateToken;
        }
        else
        {
            *resolvedToken = existing->second;
        }
        return true;
    }

    std::mutex mutex;
    std::map<std::string, int> visitors;
    std::map<std::string, std::string> bootstraps;
    bool saveAvailable;
    bool claimAvailable;
    int saveCalls;
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
    CHECK(service.valid(kTokenA));

    ar::VisitorBootstrapResult retry = service.bootstrap("", "page-first");
    CHECK(retry.status == ar::VisitorBootstrapResult::kOk);
    CHECK(retry.token == first.token);

    ar::VisitorBootstrapResult refreshed = service.bootstrap(kTokenA, "page-refresh");
    CHECK(refreshed.status == ar::VisitorBootstrapResult::kOk);
    CHECK(refreshed.token == kTokenA);

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
    for (size_t index = 0; index < results.size(); ++index)
    {
        CHECK(results[index].status == ar::VisitorBootstrapResult::kOk);
        CHECK(results[index].token == results[0].token);
    }
    CHECK(concurrent.valid(results[0].token));

    FakeVisitorStore refreshStore;
    refreshStore.visitors[kTokenA] = 7;
    ar::VisitorSessionService refreshService(&refreshStore, tokenA);
    CHECK(refreshService.refresh(kTokenA));
    CHECK(refreshStore.visitors[kTokenA] == 1800);
    refreshStore.visitors.erase(kTokenA);
    CHECK(refreshService.refresh(kTokenA));
    CHECK(refreshStore.visitors[kTokenA] == 1800);
    CHECK(!refreshService.refresh("short"));

    FakeVisitorStore recoveredStore;
    std::atomic<unsigned int> recoveredSequence(0);
    ar::VisitorSessionService recoveredService(
        &recoveredStore,
        [&recoveredSequence]() {
            return recoveredSequence.fetch_add(1) == 0
                ? std::string(kTokenA) : std::string(kTokenB);
        });
    const ar::VisitorBootstrapResult recoveredFirst =
        recoveredService.bootstrap("", "page-lost-visitor-key");
    CHECK(recoveredFirst.status == ar::VisitorBootstrapResult::kOk);
    recoveredStore.visitors.erase(kTokenA);
    const ar::VisitorBootstrapResult recoveredRetry =
        recoveredService.bootstrap("", "page-lost-visitor-key");
    CHECK(recoveredRetry.status == ar::VisitorBootstrapResult::kOk);
    CHECK(recoveredRetry.token == kTokenA);
    CHECK(recoveredStore.visitors.find(kTokenA) != recoveredStore.visitors.end());

    return 0;
}
