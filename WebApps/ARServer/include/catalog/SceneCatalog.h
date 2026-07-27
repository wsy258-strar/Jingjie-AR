// 内置场景目录：提供只读的全景资源元数据，不承担用户互动或在线状态。
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
