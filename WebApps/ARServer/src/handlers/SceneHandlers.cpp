// 场景目录和详情 HTTP 处理实现。
#include <handlers/SceneHandlers.h>

#include <catalog/ExhibitionCatalog.h>
#include <utils/ApiError.h>
#include <utils/ApiResponse.h>
#include <utils/JsonUtil.h>

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace ar {

namespace {

void configureJsonNumberFormatting(std::ostringstream* output)
{
    output->imbue(std::locale::classic());
    *output << std::setprecision(std::numeric_limits<double>::digits10 + 1);
}

void writeString(std::ostringstream* output, const std::string& value)
{
    *output << '"' << JsonUtil::escape(value) << '"';
}

std::string jsonSceneSummary(const ExhibitionScene& scene)
{
    std::ostringstream output;
    output << "{\"sceneId\":";
    writeString(&output, scene.id);
    output << ",\"panoId\":";
    writeString(&output, scene.panoId);
    output << ",\"name\":";
    writeString(&output, scene.name);
    output << ",\"previewUrl\":";
    writeString(&output, scene.previewUrl);
    output << ",\"thumbnailUrl\":";
    writeString(&output, scene.thumbnailUrl);
    output << '}';
    return output.str();
}

std::string jsonHotspot(const HotspotInfo& hotspot)
{
    std::ostringstream output;
    configureJsonNumberFormatting(&output);
    output << "{\"hotspotId\":";
    writeString(&output, hotspot.id);
    output << ",\"type\":";
    writeString(&output, hotspot.type);
    output << ",\"title\":";
    writeString(&output, hotspot.title);
    output << ",\"ath\":" << hotspot.ath
           << ",\"atv\":" << hotspot.atv
           << ",\"iconUrl\":";
    writeString(&output, hotspot.iconUrl);
    output << ",\"renderable\":" << (hotspot.type == "inactive" ? "false" : "true");
    if (!hotspot.targetSceneId.empty())
    {
        output << ",\"targetSceneId\":";
        writeString(&output, hotspot.targetSceneId);
    }
    if (!hotspot.artworkId.empty())
    {
        output << ",\"artworkId\":";
        writeString(&output, hotspot.artworkId);
    }
    if (!hotspot.text.empty())
    {
        output << ",\"text\":";
        writeString(&output, hotspot.text);
    }
    output << '}';
    return output.str();
}

std::string jsonSceneDetail(const ExhibitionScene& scene)
{
    std::ostringstream output;
    configureJsonNumberFormatting(&output);
    output << "{\"sceneId\":";
    writeString(&output, scene.id);
    output << ",\"panoId\":";
    writeString(&output, scene.panoId);
    output << ",\"name\":";
    writeString(&output, scene.name);
    output << ",\"previewUrl\":";
    writeString(&output, scene.previewUrl);
    output << ",\"cubeUrl\":";
    writeString(&output, scene.cubeUrl);
    output << ",\"thumbnailUrl\":";
    writeString(&output, scene.thumbnailUrl);
    output << ",\"view\":{\"hlookat\":" << scene.hlookat
           << ",\"vlookat\":" << scene.vlookat
           << ",\"fov\":" << scene.fov
           << "},\"music\":{\"url\":";
    writeString(&output, scene.musicUrl);
    output << ",\"volume\":" << scene.musicVolume
           << ",\"autoplay\":" << (scene.musicAutoplay ? "true" : "false")
           << ",\"loop\":" << (scene.musicLoop ? "true" : "false")
           << "},\"hotspots\":[";
    for (std::vector<HotspotInfo>::const_iterator it = scene.hotspots.begin();
         it != scene.hotspots.end(); ++it)
    {
        if (it != scene.hotspots.begin()) output << ',';
        output << jsonHotspot(*it);
    }
    output << "]}";
    return output.str();
}

} // namespace

SceneHandlers::SceneHandlers(const ExhibitionCatalog* catalog)
    : catalog_(catalog)
{
}

void SceneHandlers::list(const HttpRequest& request, HttpResponse* response) const
{
    if (!catalog_)
    {
        *response = makeApiError(HttpResponse::k503ServiceUnavailable,
                                 "SERVICE_UNAVAILABLE",
                                 "exhibition catalog unavailable",
                                 request.attribute("request_id"));
        return;
    }
    std::ostringstream output;
    output << "{\"defaultSceneId\":";
    writeString(&output, catalog_->defaultSceneId());
    output << ",\"scenes\":[";
    const std::vector<ExhibitionScene>& scenes = catalog_->scenes();
    for (std::vector<ExhibitionScene>::const_iterator it = scenes.begin(); it != scenes.end(); ++it)
    {
        if (it != scenes.begin()) output << ',';
        output << jsonSceneSummary(*it);
    }
    output << "]}";
    *response = makeApiSuccess(output.str());
}

void SceneHandlers::get(const HttpRequest& request, HttpResponse* response) const
{
    if (!catalog_)
    {
        *response = makeApiError(HttpResponse::k503ServiceUnavailable,
                                 "SERVICE_UNAVAILABLE",
                                 "exhibition catalog unavailable",
                                 request.attribute("request_id"));
        return;
    }
    const std::string id = request.pathParameter("sceneId");
    const ExhibitionScene* scene = catalog_->findScene(id);
    if (!scene)
    {
        *response = makeApiError(HttpResponse::k404NotFound,
                                 "SCENE_NOT_FOUND",
                                 "scene not found",
                                 request.attribute("request_id"));
        return;
    }
    *response = makeApiSuccess(jsonSceneDetail(*scene));
}

void SceneHandlers::interactions(const HttpRequest& request, HttpResponse* response)
{
    *response = makeApiError(HttpResponse::k501NotImplemented,
                             "INTERACTIONS_NOT_IMPLEMENTED",
                             "interactions are not implemented",
                             request.attribute("request_id"));
}
}
