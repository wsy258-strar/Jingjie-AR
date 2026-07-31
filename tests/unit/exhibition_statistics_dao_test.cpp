#include "TestSupport.h"

#include <db/ExhibitionStatisticsDAO.h>

namespace {

void testUnavailablePoolCompletesCallbacksOnceWithSafeDefaults()
{
    ExhibitionStatisticsDAO dao(0);
    int callbacks = 0;

    dao.incrementAndRead("museum", [&callbacks](bool ok, uint64_t count) {
        CHECK(!ok);
        CHECK(count == 0);
        ++callbacks;
    });
    dao.read("museum", [&callbacks](bool ok, uint64_t count) {
        CHECK(!ok);
        CHECK(count == 0);
        ++callbacks;
    });

    CHECK(callbacks == 2);
}

void testEmptyExhibitionIdIsRejectedWithoutSubmittingWork()
{
    ExhibitionStatisticsDAO dao(0);
    int callbacks = 0;
    dao.incrementAndRead("", [&callbacks](bool ok, uint64_t count) {
        CHECK(!ok);
        CHECK(count == 0);
        ++callbacks;
    });
    CHECK(callbacks == 1);
}

} // namespace

int main()
{
    testUnavailablePoolCompletesCallbacksOnceWithSafeDefaults();
    testEmptyExhibitionIdIsRejectedWithoutSubmittingWork();
    return 0;
}
