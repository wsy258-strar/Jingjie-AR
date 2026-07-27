// 固定场景目录实现：资源路径由部署静态根目录解释，目录本身不访问数据库。
#include <catalog/SceneCatalog.h>

namespace ar {
namespace {

const std::vector<SceneInfo> kScenes = {
    {"docklands", "码头区", "/assets/panoramas/docklands_02_8k.webp", "/assets/panoramas-preview/docklands_02_2048.webp", "/assets/thumbnail/docklands_02_8k.webp", ""},
    {"golden-bay", "黄金海湾", "/assets/panoramas/golden_bay_8k.webp", "/assets/panoramas-preview/golden_bay_2048.webp", "/assets/thumbnail/golden-bay-120.webp", ""},
    {"graaff-reinet-cathedral", "格拉夫-里内特大教堂", "/assets/panoramas/graaff_reinet_groote_kerk_8k.webp", "/assets/panoramas-preview/graaff_reinet_groote_kerk_2048.webp", "/assets/thumbnail/graaff_reinet_groote_kerk_8k.webp", ""},
    {"illovo-beach", "伊洛沃海滩", "/assets/panoramas/illovo_beach_balcony_8k.webp", "/assets/panoramas-preview/illovo_beach_balcony_2048.webp", "/assets/thumbnail/illovo_beach_balcony_8k.webp", ""},
    {"little-paris", "小巴黎埃菲尔铁塔", "/assets/panoramas/little_paris_eiffel_tower_8k.webp", "/assets/panoramas-preview/little_paris_eiffel_tower_2048.webp", "/assets/thumbnail/little_paris_eiffel_tower_8k.webp", ""},
    {"san-giuseppe-bridge", "圣朱塞佩桥", "/assets/panoramas/san_giuseppe_bridge_16k.webp", "/assets/panoramas-preview/san_giuseppe_bridge_2048.webp", "/assets/thumbnail/san_giuseppe_bridge_16k.webp", ""},
    {"venetian-crossroads", "威尼斯十字路口", "/assets/panoramas/venetian_crossroads_16k.webp", "/assets/panoramas-preview/venetian_crossroads_2048.webp", "/assets/thumbnail/venetian_crossroads_16k.webp", ""},
    {"vignaioli", "维尼亚约利", "/assets/panoramas/vignaioli_16k.webp", "/assets/panoramas-preview/vignaioli_2048.webp", "/assets/thumbnail/vignaioli_16k.webp", ""}
};

} // namespace

const std::vector<SceneInfo>& SceneCatalog::all()
{
    return kScenes;
}

const SceneInfo* SceneCatalog::find(const std::string& id)
{
    const std::vector<SceneInfo>& scenes = all();
    for (std::vector<SceneInfo>::const_iterator it = scenes.begin(); it != scenes.end(); ++it)
    {
        if (it->id == id) return &*it;
    }
    return 0;
}

} // namespace ar
