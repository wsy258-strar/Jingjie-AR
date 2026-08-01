#include "TestSupport.h"

#include <catalog/ExhibitionCatalog.h>
#include <services/ArtworkInteractionService.h>
#include <services/SessionService.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace {

class RecordingSessionStore : public ar::SessionStore
{
public:
    RecordingSessionStore() : findCalls(0) {}

    void find(const std::string& token, const SessionCallback& callback) override
    {
        ++findCalls;
        if (token == "valid-token")
            callback(std::shared_ptr<Session>(new Session(1, token, 7, "", 1)));
        else if (token == "inactive-token")
            callback(std::shared_ptr<Session>(new Session(2, token, 8, "", 0)));
        else
            callback(std::shared_ptr<Session>());
    }

    void enter(uint64_t, const std::string&, const BoolCallback& callback) override { callback(false); }
    void exit(uint64_t, const BoolCallback& callback) override { callback(false); }

    int findCalls;
};

class RecordingDAO : public ArtworkInteractionDAO
{
public:
    RecordingDAO()
        : ArtworkInteractionDAO(0), likeCalls(0), commentCalls(0), listCalls(0),
          lastLimit(0), duplicateLike(false) {}

    void like(const std::string&, uint64_t, const LikeCallback& callback) override
    {
        ++likeCalls;
        callback(true, !duplicateLike, 1);
    }

    void createComment(const std::string&, uint64_t, const std::string&,
                       const CommentCallback& callback) override
    {
        ++commentCalls;
        callback(true, 23);
    }

    void listComments(const std::string&, uint64_t, uint32_t limit,
                      const CommentsCallback& callback) override
    {
        ++listCalls;
        lastLimit = limit;
        callback(true, std::vector<ArtworkComment>(), 0);
    }

    int likeCalls;
    int commentCalls;
    int listCalls;
    uint32_t lastLimit;
    bool duplicateLike;
};

class DelayedSessionStore : public ar::SessionStore
{
public:
    void find(const std::string&, const SessionCallback& callback) override
    {
        pending = callback;
    }
    void enter(uint64_t, const std::string&, const BoolCallback& callback) override { callback(false); }
    void exit(uint64_t, const BoolCallback& callback) override { callback(false); }

    void complete()
    {
        pending(std::shared_ptr<Session>(new Session(1, "valid-token", 7, "", 1)));
    }

    SessionCallback pending;
};

class DelayedDAO : public ArtworkInteractionDAO
{
public:
    DelayedDAO() : ArtworkInteractionDAO(0) {}

    void like(const std::string&, uint64_t, const LikeCallback& callback) override
    {
        pending = callback;
    }

    void complete()
    {
        pending(true, true, 1);
    }

    LikeCallback pending;
};

class BlockingDAO : public ArtworkInteractionDAO
{
public:
    BlockingDAO() : ArtworkInteractionDAO(0), entered(false), released(false) {}

    void like(const std::string&, uint64_t, const LikeCallback& callback) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            entered = true;
        }
        condition.notify_all();
        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [this] { return released; });
        }
        callback(true, true, 1);
    }

    void waitUntilEntered()
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this] { return entered; });
    }

    void allowReturn()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            released = true;
        }
        condition.notify_all();
    }

    std::mutex mutex;
    std::condition_variable condition;
    bool entered;
    bool released;
};

std::unique_ptr<ar::ExhibitionCatalog> loadCatalog()
{
    std::vector<std::string> errors;
    std::unique_ptr<ar::ExhibitionCatalog> catalog = ar::ExhibitionCatalog::load(
        "WebApps/ARServer/config/exhibition.json",
        "WebApps/ARServer/www",
        &errors);
    CHECK(catalog.get() != 0);
    CHECK(errors.empty());
    return catalog;
}

std::string knownArtworkId(const ar::ExhibitionCatalog& catalog)
{
    const std::vector<ar::ExhibitionScene>& scenes = catalog.scenes();
    for (std::vector<ar::ExhibitionScene>::const_iterator scene = scenes.begin();
         scene != scenes.end(); ++scene)
        for (std::vector<ar::HotspotInfo>::const_iterator hotspot = scene->hotspots.begin();
             hotspot != scene->hotspots.end(); ++hotspot)
            if (!hotspot->artworkId.empty()) return hotspot->artworkId;
    CHECK(false);
    return std::string();
}

void testUnknownArtworkWinsBeforeAuthentication()
{
    std::unique_ptr<ar::ExhibitionCatalog> catalog = loadCatalog();
    RecordingSessionStore store;
    ar::SessionService sessions(&store);
    RecordingDAO dao;
    ar::ArtworkInteractionService service(catalog.get(), &sessions, &dao);
    ar::ArtworkInteractionResult result;

    service.like("", "missing-artwork", [&result](const ar::ArtworkInteractionResult& value) {
        result = value;
    });

    CHECK(result.status == ar::ArtworkInteractionResult::kNotFound);
    CHECK(store.findCalls == 0);
    CHECK(dao.likeCalls == 0);
}

void testAnonymousLikeIsUnauthorized()
{
    std::unique_ptr<ar::ExhibitionCatalog> catalog = loadCatalog();
    RecordingSessionStore store;
    ar::SessionService sessions(&store);
    RecordingDAO dao;
    ar::ArtworkInteractionService service(catalog.get(), &sessions, &dao);
    ar::ArtworkInteractionResult result;

    service.like("", knownArtworkId(*catalog), [&result](const ar::ArtworkInteractionResult& value) {
        result = value;
    });

    CHECK(result.status == ar::ArtworkInteractionResult::kUnauthorized);
    CHECK(dao.likeCalls == 0);
}

void testDuplicateLikeIsIdempotent()
{
    std::unique_ptr<ar::ExhibitionCatalog> catalog = loadCatalog();
    RecordingSessionStore store;
    ar::SessionService sessions(&store);
    RecordingDAO dao;
    dao.duplicateLike = true;
    ar::ArtworkInteractionService service(catalog.get(), &sessions, &dao);
    ar::ArtworkInteractionResult result;

    service.like("valid-token", knownArtworkId(*catalog),
                 [&result](const ar::ArtworkInteractionResult& value) { result = value; });

    CHECK(result.status == ar::ArtworkInteractionResult::kOk);
    CHECK(result.liked);
    CHECK(result.likeCount == 1);
    CHECK(dao.likeCalls == 1);
}

void testInactiveSessionCannotReadPersonalStateOrWriteArtwork()
{
    std::unique_ptr<ar::ExhibitionCatalog> catalog = loadCatalog();
    RecordingSessionStore store;
    ar::SessionService sessions(&store);
    RecordingDAO dao;
    ar::ArtworkInteractionService service(catalog.get(), &sessions, &dao);
    const std::string artworkId = knownArtworkId(*catalog);
    ar::ArtworkInteractionResult detail;
    ar::ArtworkInteractionResult like;

    service.detail("inactive-token", artworkId,
                   [&detail](const ar::ArtworkInteractionResult& value) { detail = value; });
    service.like("inactive-token", artworkId,
                 [&like](const ar::ArtworkInteractionResult& value) { like = value; });

    CHECK(detail.status == ar::ArtworkInteractionResult::kUnauthorized);
    CHECK(like.status == ar::ArtworkInteractionResult::kUnauthorized);
    CHECK(dao.likeCalls == 0);
}

void testCommentRejectsBlankAndOversizedBytes()
{
    std::unique_ptr<ar::ExhibitionCatalog> catalog = loadCatalog();
    RecordingSessionStore store;
    ar::SessionService sessions(&store);
    RecordingDAO dao;
    ar::ArtworkInteractionService service(catalog.get(), &sessions, &dao);
    const std::string artworkId = knownArtworkId(*catalog);
    ar::ArtworkInteractionResult blank;
    ar::ArtworkInteractionResult oversized;

    service.comment("valid-token", artworkId, " \t\r\n",
                    [&blank](const ar::ArtworkInteractionResult& value) { blank = value; });
    service.comment("valid-token", artworkId, std::string(1001, 'x'),
                    [&oversized](const ar::ArtworkInteractionResult& value) { oversized = value; });

    CHECK(blank.status == ar::ArtworkInteractionResult::kBadRequest);
    CHECK(oversized.status == ar::ArtworkInteractionResult::kBadRequest);
    CHECK(store.findCalls == 2);
    CHECK(dao.commentCalls == 0);
}

void testCommentRejectsUnicodeWhitespaceAndInvalidUtf8()
{
    std::unique_ptr<ar::ExhibitionCatalog> catalog = loadCatalog();
    RecordingSessionStore store;
    ar::SessionService sessions(&store);
    RecordingDAO dao;
    ar::ArtworkInteractionService service(catalog.get(), &sessions, &dao);
    const std::string artworkId = knownArtworkId(*catalog);
    ar::ArtworkInteractionResult fullWidthSpace;
    ar::ArtworkInteractionResult nonBreakingSpace;
    ar::ArtworkInteractionResult invalidUtf8;

    service.comment("valid-token", artworkId, "\xE3\x80\x80",
                    [&fullWidthSpace](const ar::ArtworkInteractionResult& value) {
                        fullWidthSpace = value;
                    });
    service.comment("valid-token", artworkId, "\xC2\xA0",
                    [&nonBreakingSpace](const ar::ArtworkInteractionResult& value) {
                        nonBreakingSpace = value;
                    });
    service.comment("valid-token", artworkId, std::string(1, static_cast<char>(0xff)),
                    [&invalidUtf8](const ar::ArtworkInteractionResult& value) {
                        invalidUtf8 = value;
                    });

    CHECK(fullWidthSpace.status == ar::ArtworkInteractionResult::kBadRequest);
    CHECK(nonBreakingSpace.status == ar::ArtworkInteractionResult::kBadRequest);
    CHECK(invalidUtf8.status == ar::ArtworkInteractionResult::kBadRequest);
    CHECK(dao.commentCalls == 0);
}

void testCommentAcceptsExactlyOneThousandBytes()
{
    std::unique_ptr<ar::ExhibitionCatalog> catalog = loadCatalog();
    RecordingSessionStore store;
    ar::SessionService sessions(&store);
    RecordingDAO dao;
    ar::ArtworkInteractionService service(catalog.get(), &sessions, &dao);
    ar::ArtworkInteractionResult result;

    service.comment("valid-token", knownArtworkId(*catalog), std::string(1000, 'x'),
                    [&result](const ar::ArtworkInteractionResult& value) { result = value; });

    CHECK(result.status == ar::ArtworkInteractionResult::kOk);
    CHECK(result.commentId == 23);
    CHECK(dao.commentCalls == 1);
}

void testCommentLimitIsCappedAndDefaultedToTwenty()
{
    std::unique_ptr<ar::ExhibitionCatalog> catalog = loadCatalog();
    RecordingSessionStore store;
    ar::SessionService sessions(&store);
    RecordingDAO dao;
    ar::ArtworkInteractionService service(catalog.get(), &sessions, &dao);
    const std::string artworkId = knownArtworkId(*catalog);
    ar::ArtworkInteractionResult result;

    service.listComments(artworkId, 0, 99,
                         [&result](const ar::ArtworkInteractionResult& value) { result = value; });
    CHECK(result.status == ar::ArtworkInteractionResult::kOk);
    CHECK(dao.lastLimit == 20);

    service.listComments(artworkId, 0, 0,
                         [&result](const ar::ArtworkInteractionResult& value) { result = value; });
    CHECK(result.status == ar::ArtworkInteractionResult::kOk);
    CHECK(dao.lastLimit == 20);
}

void testDelayedAuthenticationDoesNotUseDestroyedService()
{
    std::unique_ptr<ar::ExhibitionCatalog> catalog = loadCatalog();
    DelayedSessionStore store;
    ar::SessionService sessions(&store);
    RecordingDAO dao;
    std::unique_ptr<ar::ArtworkInteractionService> service(
        new ar::ArtworkInteractionService(catalog.get(), &sessions, &dao));
    ar::ArtworkInteractionResult result;
    int callbacks = 0;

    service->like(
        "valid-token", knownArtworkId(*catalog),
        [&result, &callbacks](const ar::ArtworkInteractionResult& value) {
            result = value;
            ++callbacks;
        });
    service.reset();
    store.complete();

    CHECK(callbacks == 1);
    CHECK(result.status == ar::ArtworkInteractionResult::kUnavailable);
    CHECK(dao.likeCalls == 0);
}

void testDelayedDaoDoesNotReportSuccessAfterServiceDestruction()
{
    std::unique_ptr<ar::ExhibitionCatalog> catalog = loadCatalog();
    RecordingSessionStore store;
    ar::SessionService sessions(&store);
    DelayedDAO dao;
    std::unique_ptr<ar::ArtworkInteractionService> service(
        new ar::ArtworkInteractionService(catalog.get(), &sessions, &dao));
    ar::ArtworkInteractionResult result;
    int callbacks = 0;

    service->like(
        "valid-token", knownArtworkId(*catalog),
        [&result, &callbacks](const ar::ArtworkInteractionResult& value) {
            result = value;
            ++callbacks;
        });
    service.reset();
    dao.complete();

    CHECK(callbacks == 1);
    CHECK(result.status == ar::ArtworkInteractionResult::kUnavailable);
}

void testDestructionWaitsForDaoCallToFinish()
{
    std::unique_ptr<ar::ExhibitionCatalog> catalog = loadCatalog();
    DelayedSessionStore store;
    ar::SessionService sessions(&store);
    BlockingDAO dao;
    std::unique_ptr<ar::ArtworkInteractionService> service(
        new ar::ArtworkInteractionService(catalog.get(), &sessions, &dao));
    ar::ArtworkInteractionResult result;
    service->like("valid-token", knownArtworkId(*catalog),
                  [&result](const ar::ArtworkInteractionResult& value) { result = value; });

    std::thread authentication([&store] { store.complete(); });
    dao.waitUntilEntered();
    std::atomic<bool> destroyed(false);
    std::thread destruction([&service, &destroyed] {
        service.reset();
        destroyed.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const bool returnedBeforeDao = destroyed.load(std::memory_order_acquire);
    dao.allowReturn();
    authentication.join();
    destruction.join();

    CHECK(!returnedBeforeDao);
    CHECK(destroyed.load(std::memory_order_acquire));
    CHECK(result.status == ar::ArtworkInteractionResult::kUnavailable);
}

} // namespace

int main()
{
    testUnknownArtworkWinsBeforeAuthentication();
    testAnonymousLikeIsUnauthorized();
    testDuplicateLikeIsIdempotent();
    testInactiveSessionCannotReadPersonalStateOrWriteArtwork();
    testCommentRejectsBlankAndOversizedBytes();
    testCommentRejectsUnicodeWhitespaceAndInvalidUtf8();
    testCommentAcceptsExactlyOneThousandBytes();
    testCommentLimitIsCappedAndDefaultedToTwenty();
    testDelayedAuthenticationDoesNotUseDestroyedService();
    testDelayedDaoDoesNotReportSuccessAfterServiceDestruction();
    testDestructionWaitsForDaoCallToFinish();
    return 0;
}
