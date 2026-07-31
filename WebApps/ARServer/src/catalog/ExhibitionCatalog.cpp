// 展馆配置加载实现：聚合 schema、引用、数值和静态资源错误后再决定是否发布目录。
#include <catalog/ExhibitionCatalog.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <limits.h>
#include <set>
#include <sstream>
#include <stdlib.h>
#include <sys/stat.h>

static_assert(NLOHMANN_JSON_VERSION_MAJOR == 3 &&
              NLOHMANN_JSON_VERSION_MINOR == 11 &&
              NLOHMANN_JSON_VERSION_PATCH == 3,
              "unexpected nlohmann/json version");

namespace {

typedef nlohmann::json Json;

void addError(std::vector<std::string>* errors, const std::string& context,
              const std::string& message)
{
    errors->push_back(context + ": " + message);
}

bool readString(const Json& object, const char* key, const std::string& context,
                std::string* value, std::vector<std::string>* errors, bool allowEmpty = false)
{
    Json::const_iterator found = object.find(key);
    if (found == object.end() || !found->is_string())
    {
        addError(errors, context, std::string(key) + " must be a string");
        return false;
    }
    *value = found->get<std::string>();
    if (!allowEmpty && value->empty())
    {
        addError(errors, context, std::string(key) + " must not be empty");
        return false;
    }
    return true;
}

bool readNumber(const Json& object, const char* key, const std::string& context,
                double* value, std::vector<std::string>* errors)
{
    Json::const_iterator found = object.find(key);
    if (found == object.end() || !found->is_number())
    {
        addError(errors, context, std::string(key) + " must be a number");
        return false;
    }
    *value = found->get<double>();
    if (!std::isfinite(*value))
    {
        addError(errors, context, std::string(key) + " must be finite");
        return false;
    }
    return true;
}

bool readBool(const Json& object, const char* key, const std::string& context,
              bool* value, std::vector<std::string>* errors)
{
    Json::const_iterator found = object.find(key);
    if (found == object.end() || !found->is_boolean())
    {
        addError(errors, context, std::string(key) + " must be a boolean");
        return false;
    }
    *value = found->get<bool>();
    return true;
}

void validateRange(double value, double minimum, double maximum, const std::string& context,
                   const char* name, std::vector<std::string>* errors)
{
    if (value < minimum || value > maximum)
        addError(errors, context, std::string(name) + " out of range [" +
                 std::to_string(minimum) + ", " + std::to_string(maximum) + "]");
}

bool isAssetPath(const std::string& path)
{
    if (path.compare(0, 8, "/assets/") != 0) return false;
    if (path.find('\\') != std::string::npos) return false;
    std::istringstream stream(path.substr(8));
    std::string segment;
    while (std::getline(stream, segment, '/'))
        if (segment.empty() || segment == "." || segment == "..") return false;
    return true;
}

std::string assetFile(const std::string& staticRoot, const std::string& url)
{
    if (!staticRoot.empty() && staticRoot[staticRoot.size() - 1] == '/')
        return staticRoot.substr(0, staticRoot.size() - 1) + url;
    return staticRoot + url;
}

enum AssetFileStatus
{
    kAssetFileValid,
    kAssetFileMissing,
    kAssetFileOutsideRoot
};

bool isChildPath(const std::string& root, const std::string& candidate)
{
    return candidate.size() > root.size() &&
           candidate.compare(0, root.size(), root) == 0 &&
           candidate[root.size()] == '/';
}

AssetFileStatus assetFileStatus(const std::string& staticRoot, const std::string& url)
{
    char resolved[PATH_MAX];
    if (!realpath(assetFile(staticRoot, url).c_str(), resolved)) return kAssetFileMissing;
    const std::string canonicalFile(resolved);
    if (!isChildPath(staticRoot, canonicalFile)) return kAssetFileOutsideRoot;
    struct stat metadata;
    if (::stat(canonicalFile.c_str(), &metadata) != 0 || !S_ISREG(metadata.st_mode))
        return kAssetFileMissing;
    return kAssetFileValid;
}

void validateAsset(const std::string& url, const std::string& staticRoot,
                   const std::string& context, const char* field,
                   std::vector<std::string>* errors)
{
    if (!isAssetPath(url))
    {
        addError(errors, context, std::string(field) + " must start with /assets/");
        return;
    }
    const AssetFileStatus status = assetFileStatus(staticRoot, url);
    if (status == kAssetFileOutsideRoot)
        addError(errors, context, std::string(field) + " asset escapes static root: " + url);
    else if (status == kAssetFileMissing)
        addError(errors, context, std::string(field) + " asset does not exist: " + url);
}

size_t placeholderCount(const std::string& value)
{
    size_t count = 0;
    for (size_t position = value.find("%s"); position != std::string::npos;
         position = value.find("%s", position + 2))
        ++count;
    return count;
}

bool containsUnsupportedPlaceholder(const std::string& value)
{
    for (size_t position = value.find('%'); position != std::string::npos;
         position = value.find('%', position + 2))
        if (position + 1 >= value.size() || value[position + 1] != 's') return true;
    return false;
}

void validateCubeAsset(const std::string& url, const std::string& staticRoot,
                       const std::string& context, std::vector<std::string>* errors)
{
    if (!isAssetPath(url))
    {
        addError(errors, context, "cubeUrl must start with /assets/");
        return;
    }
    if (placeholderCount(url) != 1)
    {
        addError(errors, context, "cube URL must contain exactly one %s");
        return;
    }
    if (containsUnsupportedPlaceholder(url))
    {
        addError(errors, context, "cube URL contains unsupported % placeholder");
        return;
    }
    static const char* faces[] = {"f", "b", "l", "r", "u", "d"};
    const size_t marker = url.find("%s");
    for (size_t index = 0; index < sizeof(faces) / sizeof(faces[0]); ++index)
    {
        std::string faceUrl = url;
        faceUrl.replace(marker, 2, faces[index]);
        const AssetFileStatus status = assetFileStatus(staticRoot, faceUrl);
        if (status == kAssetFileOutsideRoot)
            addError(errors, context, "cubeUrl asset escapes static root: " + faceUrl);
        else if (status == kAssetFileMissing)
            addError(errors, context, "cubeUrl asset does not exist: " + faceUrl);
    }
}

std::string canonicalStaticRoot(const std::string& staticRoot,
                                std::vector<std::string>* errors)
{
    char resolved[PATH_MAX];
    if (!realpath(staticRoot.c_str(), resolved))
    {
        errors->push_back("static root: does not exist: " + staticRoot);
        return staticRoot;
    }
    struct stat metadata;
    if (::stat(resolved, &metadata) != 0 || !S_ISDIR(metadata.st_mode))
    {
        errors->push_back("static root: must be a directory: " + staticRoot);
        return std::string(resolved);
    }
    if (std::string(resolved) == "/")
        errors->push_back("static root must not be filesystem root");
    return std::string(resolved);
}

bool addUniqueId(const std::string& id, const std::string& kind,
                 std::set<std::string>* ids, const std::string& context,
                 std::vector<std::string>* errors)
{
    if (id.empty()) return false;
    if (!ids->insert(id).second)
    {
        addError(errors, context, "duplicate " + kind + " id: " + id);
        return false;
    }
    return true;
}

std::string indexedContext(const char* collection, size_t index)
{
    return std::string(collection) + "[" + std::to_string(index) + "]";
}

void readArtwork(const Json& value, size_t index, const std::string& staticRoot,
                 std::set<std::string>* artworkIds, ar::ArtworkInfo* artwork,
                 std::vector<std::string>* errors)
{
    const std::string context = indexedContext("artworks", index);
    if (!value.is_object())
    {
        addError(errors, context, "must be an object");
        return;
    }
    if (readString(value, "artworkId", context, &artwork->id, errors))
    {
        if (artwork->id.size() > 64)
            addError(errors, context, "artworkId exceeds 64 bytes");
        addUniqueId(artwork->id, "artwork", artworkIds, context, errors);
    }
    readString(value, "title", context, &artwork->title, errors);
    readString(value, "text", context, &artwork->text, errors, true);

    Json::const_iterator images = value.find("images");
    if (images == value.end() || !images->is_array() || images->empty())
    {
        addError(errors, context, "images must be a non-empty array");
        return;
    }
    for (size_t imageIndex = 0; imageIndex < images->size(); ++imageIndex)
    {
        const std::string imageContext = context + ".images[" + std::to_string(imageIndex) + "]";
        if (!(*images)[imageIndex].is_string())
        {
            addError(errors, imageContext, "must be a string");
            continue;
        }
        const std::string image = (*images)[imageIndex].get<std::string>();
        artwork->images.push_back(image);
        validateAsset(image, staticRoot, imageContext, "image", errors);
    }
}

void readHotspot(const Json& value, size_t sceneIndex, size_t index,
                 const std::string& staticRoot, std::set<std::string>* hotspotIds,
                 ar::HotspotInfo* hotspot, std::vector<std::string>* errors)
{
    const std::string context = indexedContext("scenes", sceneIndex) +
                                ".hotspots[" + std::to_string(index) + "]";
    if (!value.is_object())
    {
        addError(errors, context, "must be an object");
        return;
    }
    if (readString(value, "hotspotId", context, &hotspot->id, errors))
        addUniqueId(hotspot->id, "hotspot", hotspotIds, context, errors);
    readString(value, "type", context, &hotspot->type, errors);
    readString(value, "title", context, &hotspot->title, errors, true);
    if (readString(value, "iconUrl", context, &hotspot->iconUrl, errors))
        validateAsset(hotspot->iconUrl, staticRoot, context, "iconUrl", errors);
    if (readNumber(value, "ath", context, &hotspot->ath, errors))
        validateRange(hotspot->ath, -180.0, 180.0, context, "ath", errors);
    if (readNumber(value, "atv", context, &hotspot->atv, errors))
        validateRange(hotspot->atv, -90.0, 90.0, context, "atv", errors);

    if (hotspot->type == "scene")
        readString(value, "targetSceneId", context, &hotspot->targetSceneId, errors);
    else if (hotspot->type == "artwork")
        readString(value, "artworkId", context, &hotspot->artworkId, errors);
    else if (hotspot->type == "text")
        readString(value, "text", context, &hotspot->text, errors);
    else if (hotspot->type != "inactive" && !hotspot->type.empty())
        addError(errors, context, "unknown hotspot type: " + hotspot->type);
}

void readScene(const Json& value, size_t index, const std::string& staticRoot,
               std::set<std::string>* sceneIds, std::set<std::string>* hotspotIds,
               ar::ExhibitionScene* scene, std::vector<std::string>* errors)
{
    const std::string context = indexedContext("scenes", index);
    if (!value.is_object())
    {
        addError(errors, context, "must be an object");
        return;
    }
    if (readString(value, "sceneId", context, &scene->id, errors))
        addUniqueId(scene->id, "scene", sceneIds, context, errors);
    readString(value, "panoId", context, &scene->panoId, errors);
    readString(value, "name", context, &scene->name, errors);
    if (readString(value, "previewUrl", context, &scene->previewUrl, errors))
        validateAsset(scene->previewUrl, staticRoot, context, "previewUrl", errors);
    if (readString(value, "cubeUrl", context, &scene->cubeUrl, errors))
        validateCubeAsset(scene->cubeUrl, staticRoot, context, errors);
    if (readString(value, "thumbnailUrl", context, &scene->thumbnailUrl, errors))
        validateAsset(scene->thumbnailUrl, staticRoot, context, "thumbnailUrl", errors);
    if (readString(value, "musicUrl", context, &scene->musicUrl, errors))
        validateAsset(scene->musicUrl, staticRoot, context, "musicUrl", errors);
    if (readNumber(value, "musicVolume", context, &scene->musicVolume, errors))
        validateRange(scene->musicVolume, 0.0, 1.0, context, "musicVolume", errors);
    readBool(value, "musicAutoplay", context, &scene->musicAutoplay, errors);
    readBool(value, "musicLoop", context, &scene->musicLoop, errors);

    Json::const_iterator view = value.find("view");
    if (view == value.end() || !view->is_object())
        addError(errors, context, "view must be an object");
    else
    {
        if (readNumber(*view, "hlookat", context + ".view", &scene->hlookat, errors))
            validateRange(scene->hlookat, -180.0, 180.0, context + ".view", "hlookat", errors);
        if (readNumber(*view, "vlookat", context + ".view", &scene->vlookat, errors))
            validateRange(scene->vlookat, -90.0, 90.0, context + ".view", "vlookat", errors);
        if (readNumber(*view, "fov", context + ".view", &scene->fov, errors) &&
            (scene->fov <= 0.0 || scene->fov > 180.0))
            addError(errors, context + ".view", "fov out of range (0, 180]");
    }

    Json::const_iterator hotspots = value.find("hotspots");
    if (hotspots == value.end() || !hotspots->is_array())
    {
        addError(errors, context, "hotspots must be an array");
        return;
    }
    for (size_t hotspotIndex = 0; hotspotIndex < hotspots->size(); ++hotspotIndex)
    {
        ar::HotspotInfo hotspot = {};
        readHotspot((*hotspots)[hotspotIndex], index, hotspotIndex, staticRoot, hotspotIds,
                    &hotspot, errors);
        scene->hotspots.push_back(hotspot);
    }
}

void validateReferences(const std::vector<ar::ExhibitionScene>& scenes,
                        const std::set<std::string>& sceneIds,
                        const std::set<std::string>& artworkIds,
                        const std::string& defaultSceneId,
                        std::vector<std::string>* errors)
{
    if (sceneIds.find(defaultSceneId) == sceneIds.end())
        addError(errors, "exhibition", "unknown default scene: " + defaultSceneId);
    for (size_t sceneIndex = 0; sceneIndex < scenes.size(); ++sceneIndex)
    {
        const ar::ExhibitionScene& scene = scenes[sceneIndex];
        for (size_t hotspotIndex = 0; hotspotIndex < scene.hotspots.size(); ++hotspotIndex)
        {
            const ar::HotspotInfo& hotspot = scene.hotspots[hotspotIndex];
            const std::string context = indexedContext("scenes", sceneIndex) +
                                        ".hotspots[" + std::to_string(hotspotIndex) + "]";
            if (hotspot.type == "scene" &&
                sceneIds.find(hotspot.targetSceneId) == sceneIds.end())
                addError(errors, context, "unknown target scene: " + hotspot.targetSceneId);
            if (hotspot.type == "artwork" &&
                artworkIds.find(hotspot.artworkId) == artworkIds.end())
                addError(errors, context, "unknown artwork: " + hotspot.artworkId);
        }
    }
}

} // namespace

namespace ar {

ExhibitionCatalog::ExhibitionCatalog() {}

std::unique_ptr<ExhibitionCatalog> ExhibitionCatalog::load(
    const std::string& configPath, const std::string& staticRoot,
    std::vector<std::string>* suppliedErrors)
{
    std::vector<std::string> localErrors;
    std::vector<std::string>* errors = suppliedErrors ? suppliedErrors : &localErrors;
    errors->clear();

    std::ifstream input(configPath.c_str(), std::ios::binary);
    if (!input)
    {
        errors->push_back("config: cannot open file: " + configPath);
        return std::unique_ptr<ExhibitionCatalog>();
    }

    Json root;
    try
    {
        input >> root;
    }
    catch (const std::exception& error)
    {
        errors->push_back(std::string("config: invalid JSON: ") + error.what());
        return std::unique_ptr<ExhibitionCatalog>();
    }
    if (!root.is_object())
    {
        errors->push_back("config: root must be an object");
        return std::unique_ptr<ExhibitionCatalog>();
    }

    std::unique_ptr<ExhibitionCatalog> catalog(new ExhibitionCatalog());
    const std::string resolvedStaticRoot = canonicalStaticRoot(staticRoot, errors);
    Json::const_iterator exhibition = root.find("exhibition");
    if (exhibition == root.end() || !exhibition->is_object())
        errors->push_back("exhibition: must be an object");
    else
    {
        readString(*exhibition, "id", "exhibition", &catalog->exhibitionId_, errors);
        readString(*exhibition, "title", "exhibition", &catalog->title_, errors);
        readString(*exhibition, "remark", "exhibition", &catalog->remark_, errors, true);
        readString(*exhibition, "defaultSceneId", "exhibition",
                   &catalog->defaultSceneId_, errors);
    }

    std::set<std::string> artworkIds;
    Json::const_iterator artworks = root.find("artworks");
    if (artworks == root.end() || !artworks->is_array())
        errors->push_back("artworks: must be an array");
    else
    {
        for (size_t index = 0; index < artworks->size(); ++index)
        {
            ArtworkInfo artwork;
            readArtwork((*artworks)[index], index, resolvedStaticRoot,
                        &artworkIds, &artwork, errors);
            catalog->artworks_.push_back(artwork);
        }
    }

    std::set<std::string> sceneIds;
    std::set<std::string> hotspotIds;
    Json::const_iterator scenes = root.find("scenes");
    if (scenes == root.end() || !scenes->is_array() || scenes->empty())
        errors->push_back("scenes: must be a non-empty array");
    else
    {
        for (size_t index = 0; index < scenes->size(); ++index)
        {
            ExhibitionScene scene = {};
            readScene((*scenes)[index], index, resolvedStaticRoot,
                      &sceneIds, &hotspotIds, &scene, errors);
            catalog->scenes_.push_back(scene);
        }
    }

    validateReferences(catalog->scenes_, sceneIds, artworkIds,
                       catalog->defaultSceneId_, errors);
    if (!errors->empty()) return std::unique_ptr<ExhibitionCatalog>();
    return catalog;
}

const std::string& ExhibitionCatalog::exhibitionId() const { return exhibitionId_; }
const std::string& ExhibitionCatalog::title() const { return title_; }
const std::string& ExhibitionCatalog::remark() const { return remark_; }
const std::string& ExhibitionCatalog::defaultSceneId() const { return defaultSceneId_; }
const std::vector<ExhibitionScene>& ExhibitionCatalog::scenes() const { return scenes_; }

const ExhibitionScene* ExhibitionCatalog::findScene(const std::string& id) const
{
    for (std::vector<ExhibitionScene>::const_iterator it = scenes_.begin();
         it != scenes_.end(); ++it)
        if (it->id == id) return &*it;
    return 0;
}

const ArtworkInfo* ExhibitionCatalog::findArtwork(const std::string& id) const
{
    for (std::vector<ArtworkInfo>::const_iterator it = artworks_.begin();
         it != artworks_.end(); ++it)
        if (it->id == id) return &*it;
    return 0;
}

} // namespace ar
