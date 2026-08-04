#include "TestSupport.h"

#include <db/SceneInteractionDAO.h>

namespace {

void testUnavailablePoolCompletesInteractionCallbacksWithSafeDefaults()
{
    SceneInteractionDAO dao(0);
    int callbacks = 0;

    dao.like("golden-bay", 7, [&callbacks](bool ok, bool changed, uint64_t count) {
        CHECK(!ok);
        CHECK(!changed);
        CHECK(count == 0);
        ++callbacks;
    });
    dao.createComment("golden-bay", 7, "hello", [&callbacks](bool ok, uint64_t id) {
        CHECK(!ok);
        CHECK(id == 0);
        ++callbacks;
    });
    dao.unlike("golden-bay", 7, [&callbacks](bool ok, bool changed, uint64_t count) {
        CHECK(!ok);
        CHECK(!changed);
        CHECK(count == 0);
        ++callbacks;
    });
    dao.summary("golden-bay", [&callbacks](bool ok, uint64_t count) {
        CHECK(!ok);
        CHECK(count == 0);
        ++callbacks;
    });
    dao.listComments("golden-bay", 0, 20,
                     [&callbacks](bool ok, const std::vector<SceneComment>& comments, uint64_t nextBefore) {
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
    testUnavailablePoolCompletesInteractionCallbacksWithSafeDefaults();
    return 0;
}
