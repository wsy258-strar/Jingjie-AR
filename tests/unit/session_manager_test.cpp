#include "TestSupport.h"

#include <http/HttpRequest.h>
#include <session/SessionManager.h>
#include <session/SessionStorage.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

typedef http::session::Session Session;
typedef http::session::SessionManager SessionManager;
typedef http::session::MemorySessionStorage MemorySessionStorage;

std::unique_ptr<http::session::SessionStorage> memoryStorage()
{
    return std::unique_ptr<http::session::SessionStorage>(new MemorySessionStorage());
}

void testCreateLoadRefreshAndDestroy()
{
    SessionManager manager(memoryStorage(), 1800);
    Session session = manager.create();
    CHECK(session.id().size() == 64);
    for (std::string::const_iterator it = session.id().begin(); it != session.id().end(); ++it)
    {
        CHECK((*it >= '0' && *it <= '9') || (*it >= 'a' && *it <= 'f'));
    }
    session.setValue("user", "ada");
    CHECK(manager.save(session));

    Session loaded;
    CHECK(manager.load(session.id(), &loaded));
    CHECK(loaded.value("user") == "ada");
    const int64_t oldExpiresAt = loaded.expiresAt();

    CHECK(manager.refresh(session.id(), &loaded));
    CHECK(loaded.expiresAt() >= oldExpiresAt);
    CHECK(manager.destroy(session.id()));
    CHECK(!manager.load(session.id(), &loaded));
}

void testSessionValuesCanBeRemovedAndCleared()
{
    Session session("id", 100000, 60);
    session.setValue("one", "1");
    session.setValue("two", "2");
    CHECK(session.remove("one"));
    CHECK(session.value("one").empty());
    session.clear();
    CHECK(session.value("two").empty());
}

void testExpiredStorageEntryIsNotLoaded()
{
    MemorySessionStorage storage;
    Session expired("expired", 1, 60);
    CHECK(storage.save(expired));
    Session loaded;
    CHECK(!storage.load("expired", &loaded));
    CHECK(!storage.remove("expired"));
}

void testMemoryStorageCopiesSessionsSafelyAcrossThreads()
{
    MemorySessionStorage storage;
    const int threadCount = 8;
    const int sessionsPerThread = 500;
    std::atomic<bool> succeeded(true);
    std::vector<std::thread> threads;

    for (int thread = 0; thread < threadCount; ++thread)
    {
        threads.push_back(std::thread([thread, &storage, &succeeded]() {
            for (int index = 0; index < sessionsPerThread; ++index)
            {
                const std::string id = std::to_string(thread) + "-" + std::to_string(index);
                Session input(id, 4102444800000LL, 60);
                input.setValue("owner", std::to_string(thread));
                Session output;
                if (!storage.save(input) || !storage.load(id, &output) ||
                    output.value("owner") != std::to_string(thread))
                {
                    succeeded.store(false);
                }
            }
        }));
    }

    for (std::vector<std::thread>::iterator it = threads.begin(); it != threads.end(); ++it)
    {
        it->join();
    }
    CHECK(succeeded.load());
}

void testValidatorUsesMemorySessionStorage()
{
    SessionManager manager(memoryStorage(), 1800);
    Session session = manager.create();
    bool valid = false;
    manager.validate(session.id(), [&valid](bool result) { valid = result; });
    CHECK(valid);
    manager.validate("missing", [&valid](bool result) { valid = result; });
    CHECK(!valid);
}

void testExtractTokenUsesAuthorizationThenCookieThenQuery()
{
    HttpRequest request;
    request.addHeader("Authorization", "Bearer authorization-token");
    request.addHeader("Cookie", "sessionId=cookie-token");
    request.setQuery("token=query-token");
    CHECK(SessionManager::extractToken(request) == "authorization-token");

    HttpRequest cookieRequest;
    cookieRequest.addHeader("Cookie", "theme=dark; sessionId=cookie-token");
    cookieRequest.setQuery("token=query-token");
    CHECK(SessionManager::extractToken(cookieRequest) == "cookie-token");

    HttpRequest queryRequest;
    queryRequest.setQuery("token=query-token");
    CHECK(SessionManager::extractToken(queryRequest) == "query-token");
}

} // namespace

int main()
{
    testCreateLoadRefreshAndDestroy();
    testSessionValuesCanBeRemovedAndCleared();
    testExpiredStorageEntryIsNotLoaded();
    testMemoryStorageCopiesSessionsSafelyAcrossThreads();
    testValidatorUsesMemorySessionStorage();
    testExtractTokenUsesAuthorizationThenCookieThenQuery();
    return 0;
}
