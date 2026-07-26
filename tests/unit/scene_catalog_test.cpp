#include "TestSupport.h"

#include <catalog/SceneCatalog.h>

namespace {

void testCatalogContainsTheEightApprovedPanoramas()
{
    const std::vector<ar::SceneInfo>& scenes = ar::SceneCatalog::all();
    CHECK(scenes.size() == 8);

    const ar::SceneInfo* goldenBay = ar::SceneCatalog::find("golden-bay");
    CHECK(goldenBay != 0);
    CHECK(goldenBay->name == "黄金海湾");
    CHECK(goldenBay->panoramaUrl == "/assets/panoramas/golden_bay_8k.webp");
    CHECK(goldenBay->thumbnailUrl == "/assets/thumbnail/golden-bay-120.webp");

    const ar::SceneInfo* docklands = ar::SceneCatalog::find("docklands");
    CHECK(docklands != nullptr);
    CHECK(docklands->thumbnailUrl == "/assets/thumbnail/docklands_02_8k.webp");
    CHECK(goldenBay->musicUrl.empty());

    CHECK(ar::SceneCatalog::find("unknown") == 0);
}

} // namespace

int main()
{
    testCatalogContainsTheEightApprovedPanoramas();
    return 0;
}
