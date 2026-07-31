#include "TestSupport.h"

#include <catalog/ExhibitionCatalog.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

static_assert(!std::is_copy_constructible<ar::ExhibitionCatalog>::value,
              "catalog must not be copy constructible");
static_assert(!std::is_copy_assignable<ar::ExhibitionCatalog>::value,
              "catalog must not be copy assignable");
static_assert(!std::is_move_constructible<ar::ExhibitionCatalog>::value,
              "catalog must not be move constructible");
static_assert(!std::is_move_assignable<ar::ExhibitionCatalog>::value,
              "catalog must not be move assignable");

namespace {

bool containsError(const std::vector<std::string>& errors, const std::string& needle)
{
    for (std::vector<std::string>::const_iterator it = errors.begin(); it != errors.end(); ++it)
        if (it->find(needle) != std::string::npos) return true;
    return false;
}

bool referencesArtwork(const ar::ExhibitionScene* scene, const std::string& artworkId)
{
    if (!scene) return false;
    for (std::vector<ar::HotspotInfo>::const_iterator it = scene->hotspots.begin();
         it != scene->hotspots.end(); ++it)
        if (it->artworkId == artworkId) return true;
    return false;
}

void checkAssetsCannotEscapeStaticRoot()
{
    char rootTemplate[] = "/tmp/exhibition-catalog-root-XXXXXX";
    char* rootPath = mkdtemp(rootTemplate);
    CHECK(rootPath != 0);
    char actualAssets[PATH_MAX];
    CHECK(realpath("WebApps/ARServer/www/assets", actualAssets) != 0);
    const std::string linkedAssets = std::string(rootPath) + "/assets";
    CHECK(symlink(actualAssets, linkedAssets.c_str()) == 0);

    std::vector<std::string> errors;
    std::unique_ptr<ar::ExhibitionCatalog> catalog =
        ar::ExhibitionCatalog::load("WebApps/ARServer/config/exhibition.json",
                                    rootPath, &errors);
    CHECK(catalog.get() == 0);
    CHECK(containsError(errors, "asset escapes static root"));

    CHECK(unlink(linkedAssets.c_str()) == 0);
    CHECK(rmdir(rootPath) == 0);
}

} // namespace

int main()
{
    std::vector<std::string> errors;
    std::unique_ptr<ar::ExhibitionCatalog> catalog =
        ar::ExhibitionCatalog::load("WebApps/ARServer/config/exhibition.json",
                                    "WebApps/ARServer/www", &errors);
    if (!catalog)
        for (std::vector<std::string>::const_iterator it = errors.begin(); it != errors.end(); ++it)
            std::cerr << *it << std::endl;
    CHECK(catalog.get() != 0);
    CHECK(errors.empty());
    CHECK(catalog->exhibitionId() == "19491365");
    CHECK(catalog->title() == "画叙勤廉·浙江美术馆馆藏作品展");
    CHECK(!catalog->remark().empty());
    CHECK(catalog->scenes().size() == 9);
    CHECK(catalog->defaultSceneId() == "76196992");
    CHECK(catalog->findScene("76196992") != 0);
    CHECK(catalog->findScene("76196992")->panoId == "15949056");
    CHECK(catalog->findScene("missing") == 0);

    const ar::ArtworkInfo* sailing = catalog->findArtwork("s_76196995_2");
    CHECK(sailing != 0);
    CHECK(sailing->title == "《启航》");
    CHECK(sailing->text.find("何红舟  黄发祥") != std::string::npos);
    CHECK(catalog->findArtwork("missing") == 0);

    CHECK(referencesArtwork(catalog->findScene("76196996"), "s_76196996_5"));
    CHECK(referencesArtwork(catalog->findScene("76196997"), "s_76196996_5"));

    errors.push_back("stale");
    std::unique_ptr<ar::ExhibitionCatalog> invalid =
        ar::ExhibitionCatalog::load("tests/fixtures/exhibition_invalid_reference.json",
                                    "WebApps/ARServer/www", &errors);
    CHECK(invalid.get() == 0);
    CHECK(!containsError(errors, "stale"));
    CHECK(containsError(errors, "duplicate artwork id"));
    CHECK(containsError(errors, "artworkId exceeds 64 bytes"));
    CHECK(containsError(errors, "duplicate scene id"));
    CHECK(containsError(errors, "duplicate hotspot id"));
    CHECK(containsError(errors, "unknown default scene"));
    CHECK(containsError(errors, "unknown target scene"));
    CHECK(containsError(errors, "unknown artwork"));
    CHECK(containsError(errors, "out of range"));
    CHECK(containsError(errors, "must start with /assets/"));
    CHECK(containsError(errors, "cube URL must contain exactly one %s"));
    CHECK(containsError(errors, "cube URL contains unsupported % placeholder"));
    CHECK(containsError(errors, "asset does not exist"));
    CHECK(containsError(errors, "asset does not exist: /assets/pano/15949056"));
    CHECK(errors.size() > 9);
    checkAssetsCannotEscapeStaticRoot();

    errors.clear();
    CHECK(!ar::ExhibitionCatalog::load("WebApps/ARServer/config/exhibition.json",
                                       "/", &errors));
    CHECK(containsError(errors, "static root must not be filesystem root"));
    return 0;
}
