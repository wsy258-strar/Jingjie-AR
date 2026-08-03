#include "TestSupport.h"

#include <db/ArtworkInteractionDAO.h>

namespace {

void testUnavailablePoolCompletesAllCallbacksWithSafeDefaults()
{
    ArtworkInteractionDAO dao(0);
    int callbacks = 0;

    dao.like("artwork-1", 7, [&callbacks](bool ok, bool changed, uint64_t count, bool liked) {
        CHECK(!ok);
        CHECK(!changed);
        CHECK(count == 0);
        CHECK(!liked);
        ++callbacks;
    });
    dao.unlike("artwork-1", 7, [&callbacks](bool ok, bool changed, uint64_t count, bool liked) {
        CHECK(!ok);
        CHECK(!changed);
        CHECK(count == 0);
        CHECK(!liked);
        ++callbacks;
    });
    dao.summary("artwork-1", 7,
                [&callbacks](bool ok, uint64_t count, bool liked, uint64_t commentCount) {
        CHECK(!ok);
        CHECK(count == 0);
        CHECK(!liked);
        CHECK(commentCount == 0);
        ++callbacks;
    });
    dao.createComment("artwork-1", 7, "hello", [&callbacks](bool ok, uint64_t id) {
        CHECK(!ok);
        CHECK(id == 0);
        ++callbacks;
    });
    dao.listComments(
        "artwork-1", 0, 20,
        [&callbacks](bool ok, const std::vector<ArtworkComment>& comments, uint64_t nextBefore) {
            CHECK(!ok);
            CHECK(comments.empty());
            CHECK(nextBefore == 0);
            ++callbacks;
        });

    CHECK(callbacks == 5);
}

} // namespace

int main()
{
    testUnavailablePoolCompletesAllCallbacksWithSafeDefaults();
    return 0;
}
