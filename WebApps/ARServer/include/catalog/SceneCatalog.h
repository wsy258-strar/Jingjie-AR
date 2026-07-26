#pragma once

#include <string>
#include <vector>

namespace ar {

struct SceneInfo
{
    std::string id;
    std::string name;
    std::string panoramaUrl;
    std::string previewUrl;
    std::string thumbnailUrl;
    std::string musicUrl;
};

class SceneCatalog
{
public:
    static const std::vector<SceneInfo>& all();
    static const SceneInfo* find(const std::string& id);
};

} // namespace ar
