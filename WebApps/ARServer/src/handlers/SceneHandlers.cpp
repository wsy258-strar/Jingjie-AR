#include <handlers/SceneHandlers.h>

#include <catalog/SceneCatalog.h>
#include <utils/ApiError.h>
#include <utils/JsonUtil.h>

#include <sstream>

namespace ar {

namespace {

std::string jsonScene(const SceneInfo& scene)
{
    std::ostringstream output;
    output << "{\"id\":\"" << JsonUtil::escape(scene.id)
           << "\",\"name\":\"" << JsonUtil::escape(scene.name)
           << "\",\"panorama_url\":\"" << JsonUtil::escape(scene.panoramaUrl)
           << "\",\"thumbnail_url\":\"" << JsonUtil::escape(scene.thumbnailUrl)
           << "\",\"music_url\":";
    if (scene.musicUrl.empty()) output << "null";
    else output << "\"" << JsonUtil::escape(scene.musicUrl) << "\"";
    output << "}";
    return output.str();
}

} // namespace

void SceneHandlers::list(const HttpRequest&, HttpResponse* response) {
    std::ostringstream output;
    output << "{\"scenes\":[";
    const std::vector<SceneInfo>& scenes = SceneCatalog::all();
    for (std::vector<SceneInfo>::const_iterator it = scenes.begin(); it != scenes.end(); ++it)
    {
        if (it != scenes.begin()) output << ',';
        output << jsonScene(*it);
    }
    output << "]}";
    response->setContentType("application/json; charset=utf-8");
    response->setBody(output.str());
}
void SceneHandlers::get(const HttpRequest& request, HttpResponse* response) {
    const std::string id = request.pathParameter("sceneId");
    const SceneInfo* scene = SceneCatalog::find(id);
    if (!scene)
    {
        *response = makeApiError(HttpResponse::k404NotFound, "SCENE_NOT_FOUND", "scene not found");
        return;
    }
    response->setContentType("application/json; charset=utf-8");
    response->setBody(jsonScene(*scene));
}
void SceneHandlers::interactions(const HttpRequest&, HttpResponse* response) {
    response->setStatusCode(HttpResponse::k501NotImplemented);
    response->setContentType("application/json; charset=utf-8");
    response->setBody("{\"error\":\"interactions are not implemented\",\"code\":\"INTERACTIONS_NOT_IMPLEMENTED\"}");
}
}
