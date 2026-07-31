// 展馆配置目录：启动时一次性加载并验证，加载成功后只提供只读查询接口。
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ar {

struct HotspotInfo
{
    std::string id;
    std::string type;
    std::string title;
    std::string iconUrl;
    std::string targetSceneId;
    std::string artworkId;
    std::string text;
    double ath;
    double atv;
};

struct ExhibitionScene
{
    std::string id;
    std::string panoId;
    std::string name;
    std::string previewUrl;
    std::string cubeUrl;
    std::string thumbnailUrl;
    std::string musicUrl;
    double hlookat;
    double vlookat;
    double fov;
    double musicVolume;
    bool musicAutoplay;
    bool musicLoop;
    std::vector<HotspotInfo> hotspots;
};

struct ArtworkInfo
{
    std::string id;
    std::string title;
    std::string text;
    std::vector<std::string> images;
};

class ExhibitionCatalog
{
public:
    static std::unique_ptr<ExhibitionCatalog> load(
        const std::string& configPath,
        const std::string& staticRoot,
        std::vector<std::string>* errors);

    const std::string& exhibitionId() const;
    const std::string& title() const;
    const std::string& remark() const;
    const std::string& defaultSceneId() const;
    const std::vector<ExhibitionScene>& scenes() const;
    const ExhibitionScene* findScene(const std::string& id) const;
    const ArtworkInfo* findArtwork(const std::string& id) const;

private:
    ExhibitionCatalog();
    ExhibitionCatalog(const ExhibitionCatalog&) = delete;
    ExhibitionCatalog& operator=(const ExhibitionCatalog&) = delete;
    ExhibitionCatalog(ExhibitionCatalog&&) = delete;
    ExhibitionCatalog& operator=(ExhibitionCatalog&&) = delete;

    std::string exhibitionId_;
    std::string title_;
    std::string remark_;
    std::string defaultSceneId_;
    std::vector<ExhibitionScene> scenes_;
    std::vector<ArtworkInfo> artworks_;
};

} // namespace ar
