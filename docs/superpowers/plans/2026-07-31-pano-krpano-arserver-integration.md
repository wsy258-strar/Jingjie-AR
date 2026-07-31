# krpano 展馆前后端一体化实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 `pano/html/index.html` 的原始展馆内容替换现有 A-Frame 页面，保留 krpano 播放能力，并接入 ARServer 的匿名访客、展馆在线人数、总浏览量和作品级点赞评论。

**Architecture:** Nginx 直接发送 `/assets/*`，ARServer 读取启动期不可变的 `exhibition.json` 并提供 `/api/*`。Redis 维护匿名 Visitor Token 与展馆级在线集合，MySQL 保存不去重总浏览量和作品互动；浏览器使用独立的 Visitor Token 与 User Token。

**Tech Stack:** C++11、CMake、nlohmann/json 3.11.3、MySQL、Redis/hiredis、原生 ES Modules、krpano 1.19、Python 3 标准库迁移脚本、Git LFS、Nginx、systemd。

## Global Constraints

- `pano/html/index.html` 是场景、热点、作品正文、图片和音乐等业务内容的唯一来源。
- 只允许规范化字段类型、调整部署资源前缀；不得杜撰、润色或补齐原文件没有的业务内容。
- 配置必须包含 9 个场景、54 个立方体面、9 张预览图和 9 张缩略图；每个原热点必须可追溯。
- 原始 `pano/` 目录只读，不直接作为生产静态根目录。
- 游客无需登录即可浏览；点赞和评论必须使用 User Token。
- 在线人数表示整个展馆，不按场景拆分；场景切换不得改变在线人数。
- 每次完整页面初始化总浏览量增加一次，不按 Visitor Token、用户、IP 或设备去重。
- 同一次初始化的传输重试使用同一 `bootstrapRequestId`，不得重复增加浏览量。
- 点赞和评论归属于作品；相同作品在多个热点出现时共享互动数据。
- Visitor Token 使用 `X-Visitor-Token`，User Token 使用 `Authorization: Bearer <token>`，两者保存于 `sessionStorage`。
- 新接口统一返回 `{success,data,message}`；失败时额外返回稳定的 `code` 和 `requestId`。
- 故障降级必须保证目录失败可重试、场景失败保留当前画面、高清失败保留预览、统计与在线失败不阻断浏览、评论失败保留输入内容。
- 继续使用 C++11，不在生产前端引入 Node.js、React、Vue 或原 720 平台业务包。
- 旧 `scene_likes`、`scene_comments` 表暂不删除，数据库迁移必须可回滚。
- krpano 授权运行文件不提交到无权分发的公开仓库，通过受控部署步骤提供。
- 二进制全景、插图和音频使用 Git LFS；禁止把这些文件作为普通 Git Blob 提交。

---

## 文件结构与职责

### 数据与配置

- `scripts/migrate_pano.py`：从原始 HTML 确定性提取展馆数据、复制允许提交的素材并生成清单。
- `scripts/provision_krpano.sh`：从已授权源目录安装唯一一套 krpano 运行文件。
- `WebApps/ARServer/config/exhibition.json`：迁移生成的生产展馆配置。
- `WebApps/ARServer/config/assets-manifest.json`：资源相对路径、大小和 SHA-256。
- `third_party/nlohmann/json.hpp`：固定为 nlohmann/json 3.11.3 单头文件。

### 后端

- `WebApps/ARServer/include/catalog/ExhibitionCatalog.h`、`src/catalog/ExhibitionCatalog.cpp`：配置模型、加载、校验和只读查询。
- `WebApps/ARServer/include/utils/ApiResponse.h`、`src/utils/ApiResponse.cpp`：统一成功/失败响应。
- `WebApps/ARServer/include/handlers/SceneHandlers.h`、`src/handlers/SceneHandlers.cpp`：场景目录和详情。
- `include/db/ArtworkInteractionDAO.h`、`src/db/ArtworkInteractionDAO.cpp`：作品点赞评论 SQL。
- `WebApps/ARServer/include/services/ArtworkInteractionService.h`、`src/services/ArtworkInteractionService.cpp`：作品校验、用户认证与互动编排。
- `WebApps/ARServer/include/handlers/ArtworkInteractionHandlers.h`、`src/handlers/ArtworkInteractionHandlers.cpp`：作品 HTTP 接口。
- `WebApps/ARServer/include/services/VisitorSessionService.h`、`src/services/VisitorSessionService.cpp`：匿名 Token、初始化请求幂等和 Redis 访客会话。
- `WebApps/ARServer/include/services/PresenceService.h`、`src/services/PresenceService.cpp`：展馆级在线集合。
- `include/db/ExhibitionStatisticsDAO.h`、`src/db/ExhibitionStatisticsDAO.cpp`：总浏览量原子递增和读取。
- `WebApps/ARServer/include/handlers/VisitorHandlers.h`、`src/handlers/VisitorHandlers.cpp`：匿名初始化、心跳、退出、在线人数和浏览量接口。
- `WebApps/ARServer/include/ARServer.h`、`src/ARServer.cpp`、`src/main.cpp`：依赖装配与路由。

### 前端

- `WebApps/ARServer/www/index.html`：轻量页面骨架。
- `WebApps/ARServer/www/css/museum.css`：响应式布局、弹窗和加载状态。
- `WebApps/ARServer/www/js/api-client.js`：统一 fetch、双 Token 请求头和 API 错误。
- `WebApps/ARServer/www/js/visitor-session.js`：匿名初始化、30 秒心跳和退出。
- `WebApps/ARServer/www/js/auth-session.js`：登录弹窗、User Token 与待执行操作重试。
- `WebApps/ARServer/www/js/krpano-adapter.js`：播放器初始化、XML 转义、场景和热点装载。
- `WebApps/ARServer/www/js/artwork-modal.js`：作品详情、点赞和评论 UI。
- `WebApps/ARServer/www/js/museum-app.js`：启动流程和页面状态协调。

---

### Task 1: 确定性迁移展馆数据与静态素材

**Files:**
- Create: `scripts/migrate_pano.py`
- Create: `scripts/provision_krpano.sh`
- Create: `tests/integration/pano_migration_test.sh`
- Create: `.gitattributes`
- Modify: `.gitignore`
- Generate: `WebApps/ARServer/config/exhibition.json`
- Generate: `WebApps/ARServer/config/assets-manifest.json`
- Generate: `WebApps/ARServer/www/assets/pano/`
- Generate: `WebApps/ARServer/www/assets/illustration/`
- Generate: `WebApps/ARServer/www/assets/hotspot/`
- Generate: `WebApps/ARServer/www/assets/music/`

**Interfaces:**
- Consumes: `pano/html/index.html` 和 `pano/html/assets/`。
- Produces: UTF-8 JSON 配置；场景键为原 `scene.id`，`panoId` 单独保留；作品键为首次出现该作品的原热点 ID。

- [ ] **Step 1: 写迁移失败测试**

```bash
#!/usr/bin/env bash
set -euo pipefail
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT
python3 scripts/migrate_pano.py \
  --source pano/html \
  --config "$tmp_dir/exhibition.json" \
  --assets "$tmp_dir/assets" \
  --manifest "$tmp_dir/assets-manifest.json"
python3 - "$tmp_dir" <<'PY'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
data = json.loads((root / "exhibition.json").read_text(encoding="utf-8"))
def _paths(value):
    if isinstance(value, dict):
        for key, child in value.items():
            if key in {"previewUrl", "cubeUrl", "thumbnailUrl", "musicUrl",
                       "iconUrl", "url"} and isinstance(child, str) and child:
                yield child
            elif key == "images" and isinstance(child, list):
                yield from (item for item in child if isinstance(item, str))
            yield from _paths(child)
    elif isinstance(value, list):
        for child in value:
            yield from _paths(child)
assert data["exhibition"]["id"] == "19491365"
assert data["exhibition"]["title"] == "画叙勤廉·浙江美术馆馆藏作品展"
assert data["exhibition"]["defaultSceneId"] == "76196992"
assert len(data["scenes"]) == 9
assert sum(len(scene["hotspots"]) for scene in data["scenes"]) == 41
assert len(list((root / "assets/pano").glob("*/*_[bdflru].jpg"))) == 54
assert len(list((root / "assets/pano").glob("*/preview.jpg"))) == 9
assert len(list((root / "assets/pano").glob("*/thumb.jpg"))) == 9
qihang = next(a for a in data["artworks"] if a["title"] == "《启航》")
assert qihang["images"] == ["/assets/illustration/qihang.jpg"]
assert "何红舟  黄发祥" in qihang["text"]
assert all(not key.startswith("http") for key in _paths(data))
PY
```

在测试内补充 `_paths` 递归函数，收集以 `Url`、`url`、`images`、`preview`、`cubeUrl` 命名的资源字段，确保全部使用 `/assets/`。

- [ ] **Step 2: 运行测试并确认失败**

Run: `bash tests/integration/pano_migration_test.sh`
Expected: FAIL，提示 `scripts/migrate_pano.py` 不存在。

- [ ] **Step 3: 实现受限 JS 数据提取器**

迁移脚本只使用 Python 3 标准库。它提取名称满足 `[A-Za-z][A-Za-z0-9_]*Text` 的 `const` 字符串声明和 `window.__INITIAL_STATE__`，解析字符串拼接、模板字符串和尾随逗号，不执行 HTML 中的任意 JavaScript。

热点映射必须按载荷而非只按数字 `type`：

```python
def normalize_hotspot(raw, pano_to_scene, artwork_by_signature):
    data = raw.get("data")
    base = {
        "hotspotId": str(raw["id"]),
        "title": raw.get("title", ""),
        "ath": float(raw["ath"]),
        "atv": float(raw["atv"]),
        "iconUrl": asset_url(raw.get("iconUrl", "")),
    }
    if isinstance(data, dict) and str(data.get("panoId", "")) not in ("", "0"):
        base.update(type="scene", targetSceneId=pano_to_scene[str(data["panoId"])])
        return base
    if isinstance(data, list) and data and all("image" in item and "text" in item for item in data):
        signature = json.dumps(
            {"title": raw.get("title", ""), "data": data},
            ensure_ascii=False, sort_keys=True)
        artwork_id = artwork_by_signature.setdefault(signature, str(raw["id"]))
        base.update(type="artwork", artworkId=artwork_id)
        return base
    if raw.get("type") == 4 and isinstance(data, str):
        base.update(type="text", text=data)
        return base
    if isinstance(data, dict) and str(data.get("panoId", "")) == "0":
        base.update(type="inactive", rawData=data)
        return base
    raise ValueError("无法识别热点 %s" % raw.get("id"))
```

相同标题、图片数组和正文形成相同签名，因此重复作品热点引用同一 `artworkId`。`inactive` 热点写入配置但前端不渲染。所有正文保留换行和原字词，不做纠错。

- [ ] **Step 4: 实现资源复制、清单和 LFS 规则**

`.gitattributes`：

```gitattributes
WebApps/ARServer/www/assets/pano/**/*.jpg filter=lfs diff=lfs merge=lfs -text
WebApps/ARServer/www/assets/illustration/**/*.jpg filter=lfs diff=lfs merge=lfs -text
WebApps/ARServer/www/assets/music/**/*.mp3 filter=lfs diff=lfs merge=lfs -text
```

`.gitignore` 增加：

```gitignore
# krpano 授权运行文件由 scripts/provision_krpano.sh 安装
WebApps/ARServer/www/assets/krp/runtime/
```

`assets-manifest.json` 的每项固定为：

```json
{"path":"pano/15949056/preview.jpg","size":12345,"sha256":"64位小写十六进制"}
```

`provision_krpano.sh` 接受一个参数，验证并复制：

```bash
source_root=${1:?usage: provision_krpano.sh /path/to/pano/html/assets/krp}
test -f "$source_root/1.19-pr10/player_krp_v2.js"
test -f "$source_root/player_offline.xml"
install -d WebApps/ARServer/www/assets/krp/runtime
install -m 0644 "$source_root/1.19-pr10/player_krp_v2.js" \
  WebApps/ARServer/www/assets/krp/runtime/player_krp_v2.js
install -m 0644 "$source_root/player_offline.xml" \
  WebApps/ARServer/www/assets/krp/runtime/player_offline.xml
```

- [ ] **Step 5: 生成产物并运行迁移测试**

Run:

```bash
python3 scripts/migrate_pano.py \
  --source pano/html \
  --config WebApps/ARServer/config/exhibition.json \
  --assets WebApps/ARServer/www/assets \
  --manifest WebApps/ARServer/config/assets-manifest.json
bash tests/integration/pano_migration_test.sh
git lfs ls-files
```

Expected: 测试输出 `PASS: pano migration`；LFS 列出新 JPG 和 MP3，不包含 krpano 授权文件。

- [ ] **Step 6: 提交**

```bash
git add .gitattributes .gitignore scripts/migrate_pano.py scripts/provision_krpano.sh
git add -f tests/integration/pano_migration_test.sh
git add WebApps/ARServer/config WebApps/ARServer/www/assets
git commit -m "迁移展馆配置与全景素材"
```

---

### Task 2: 实现不可变 ExhibitionCatalog

**Files:**
- Create: `third_party/nlohmann/json.hpp`
- Create: `WebApps/ARServer/include/catalog/ExhibitionCatalog.h`
- Create: `WebApps/ARServer/src/catalog/ExhibitionCatalog.cpp`
- Create: `tests/unit/exhibition_catalog_test.cpp`
- Create: `tests/fixtures/exhibition_invalid_reference.json`
- Modify: `WebApps/ARServer/include/config/AppConfig.h`
- Modify: `WebApps/ARServer/src/config/AppConfig.cpp`
- Modify: `tests/unit/app_config_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
struct HotspotInfo {
    std::string id, type, title, iconUrl, targetSceneId, artworkId, text;
    double ath, atv;
};
struct ExhibitionScene {
    std::string id, panoId, name, previewUrl, cubeUrl, thumbnailUrl, musicUrl;
    double hlookat, vlookat, fov, musicVolume;
    bool musicAutoplay, musicLoop;
    std::vector<HotspotInfo> hotspots;
};
struct ArtworkInfo {
    std::string id, title, text;
    std::vector<std::string> images;
};
class ExhibitionCatalog {
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
};
```

- [ ] **Step 1: 写配置与目录失败测试**

测试真实配置可加载，且断言 9 个场景、默认场景 `76196992`、`《启航》` 正文与重复作品引用；无效引用配置必须返回空指针并包含 `unknown target scene`。

```cpp
std::vector<std::string> errors;
std::unique_ptr<ar::ExhibitionCatalog> catalog =
    ar::ExhibitionCatalog::load("WebApps/ARServer/config/exhibition.json",
                                "WebApps/ARServer/www", &errors);
CHECK(catalog.get() != 0);
CHECK(catalog->scenes().size() == 9);
CHECK(catalog->defaultSceneId() == "76196992");
CHECK(catalog->findScene("76196992")->panoId == "15949056");
CHECK(catalog->findArtwork("s_76196995_1")->text.find("何红舟  黄发祥") != std::string::npos);
```

- [ ] **Step 2: 运行测试并确认失败**

Run: `cmake -S . -B build-full && cmake --build build-full --target exhibition_catalog_test -j2`
Expected: FAIL，缺少 `catalog/ExhibitionCatalog.h`。

- [ ] **Step 3: 固定 JSON 依赖并实现严格加载**

将官方 nlohmann/json `v3.11.3` 的 `single_include/nlohmann/json.hpp` 原样放入 `third_party/nlohmann/json.hpp`，代码中校验：

```cpp
static_assert(NLOHMANN_JSON_VERSION_MAJOR == 3 &&
              NLOHMANN_JSON_VERSION_MINOR == 11 &&
              NLOHMANN_JSON_VERSION_PATCH == 3,
              "unexpected nlohmann/json version");
```

加载器一次性解析文件并验证 ID 唯一、默认场景、引用、坐标、`/assets/` 路径、立方体 `%s` 模板以及实际资源。加载成功后对象不提供修改接口。

- [ ] **Step 4: 增加配置路径**

`AppConfig` 新增：

```cpp
std::string exhibitionConfig;
```

默认值为 `WebApps/ARServer/config/exhibition.json`，环境变量为 `AR_EXHIBITION_CONFIG`。将变量加入 `fromEnvironment` 白名单，并在 `app_config_test` 中断言默认值和覆盖值。

- [ ] **Step 5: 接入 CMake 并运行测试**

新增 `exhibition_catalog_test`，包含应用头文件和 `third_party`，编译 `ExhibitionCatalog.cpp`。

Run:

```bash
cmake --build build-full --target exhibition_catalog_test app_config_test -j2
ctest --test-dir build-full -R 'exhibition_catalog_test|app_config_test' --output-on-failure
```

Expected: 2/2 tests passed。

- [ ] **Step 6: 提交**

```bash
git add third_party/nlohmann/json.hpp WebApps/ARServer/include/catalog \
  WebApps/ARServer/src/catalog WebApps/ARServer/include/config/AppConfig.h \
  WebApps/ARServer/src/config/AppConfig.cpp CMakeLists.txt
git add -f tests/unit/exhibition_catalog_test.cpp tests/fixtures/exhibition_invalid_reference.json \
  tests/unit/app_config_test.cpp
git commit -m "实现展馆配置目录加载"
```

---

### Task 3: 统一 API 响应并提供场景只读接口

**Files:**
- Create: `WebApps/ARServer/include/utils/ApiResponse.h`
- Create: `WebApps/ARServer/src/utils/ApiResponse.cpp`
- Modify: `WebApps/ARServer/include/handlers/SceneHandlers.h`
- Modify: `WebApps/ARServer/src/handlers/SceneHandlers.cpp`
- Modify: `WebApps/ARServer/include/utils/ApiError.h`
- Modify: `WebApps/ARServer/src/utils/ApiError.cpp`
- Create: `tests/unit/api_response_test.cpp`
- Modify: `tests/unit/ar_handlers_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ExhibitionCatalog`。
- Produces:

```cpp
HttpResponse makeApiSuccess(const std::string& dataJson,
                            const std::string& message = std::string());
HttpResponse makeApiError(HttpResponse::HttpStatusCode status,
                          const std::string& code,
                          const std::string& message,
                          const std::string& requestId = std::string());
class SceneHandlers {
public:
    explicit SceneHandlers(const ExhibitionCatalog* catalog);
    void list(const HttpRequest&, HttpResponse*) const;
    void get(const HttpRequest&, HttpResponse*) const;
};
```

- [ ] **Step 1: 写响应与场景接口测试**

```cpp
HttpResponse ok = ar::makeApiSuccess("{\"value\":7}");
CHECK(ok.body() == "{\"success\":true,\"data\":{\"value\":7},\"message\":\"\"}");
HttpResponse error = ar::makeApiError(HttpResponse::k404NotFound,
    "SCENE_NOT_FOUND", "scene not found", "request-1");
CHECK(error.body() ==
    "{\"success\":false,\"data\":null,\"message\":\"scene not found\","
    "\"code\":\"SCENE_NOT_FOUND\",\"requestId\":\"request-1\"}");
```

场景列表断言 `data.defaultSceneId`、9 个场景摘要；详情断言 cube、preview、完整 view、music 和热点，且 `inactive` 热点仍在 API 数据中标记为不可渲染。

- [ ] **Step 2: 运行测试并确认失败**

Run: `cmake --build build-full --target api_response_test ar_handlers_test -j2`
Expected: FAIL，缺少 `ApiResponse.h` 或构造函数不匹配。

- [ ] **Step 3: 实现响应工具和实例化 SceneHandlers**

所有数据片段必须由后端序列化，字符串使用 `JsonUtil::escape`。成功响应的 `data` 是对象或数组，不能二次编码为字符串。404 使用 `SCENE_NOT_FOUND`。

- [ ] **Step 4: 运行测试**

Run: `ctest --test-dir build-full -R 'api_response_test|ar_handlers_test' --output-on-failure`
Expected: 2/2 tests passed。

- [ ] **Step 5: 提交**

```bash
git add WebApps/ARServer/include/utils WebApps/ARServer/src/utils \
  WebApps/ARServer/include/handlers/SceneHandlers.h \
  WebApps/ARServer/src/handlers/SceneHandlers.cpp CMakeLists.txt
git add -f tests/unit/api_response_test.cpp tests/unit/ar_handlers_test.cpp
git commit -m "统一接口响应并实现展馆场景查询"
```

---

### Task 4: 增加作品级点赞评论持久化

**Files:**
- Modify: `sql/jingjie_ar_schema.sql`
- Create: `include/db/ArtworkInteractionDAO.h`
- Create: `src/db/ArtworkInteractionDAO.cpp`
- Create: `WebApps/ARServer/include/services/ArtworkInteractionService.h`
- Create: `WebApps/ARServer/src/services/ArtworkInteractionService.cpp`
- Create: `WebApps/ARServer/include/handlers/ArtworkInteractionHandlers.h`
- Create: `WebApps/ARServer/src/handlers/ArtworkInteractionHandlers.cpp`
- Create: `tests/unit/artwork_interaction_dao_test.cpp`
- Create: `tests/unit/artwork_interaction_service_test.cpp`
- Create: `tests/unit/artwork_interaction_handlers_test.cpp`
- Modify: `tests/integration/jingjie_ar_schema_static_test.sh`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
struct ArtworkComment { uint64_t id; std::string username, content; };
class ArtworkInteractionDAO {
public:
    typedef std::function<void(bool, bool, uint64_t)> LikeCallback;
    typedef std::function<void(bool, uint64_t)> CommentCallback;
    typedef std::function<void(bool, uint64_t, bool)> SummaryCallback;
    typedef std::function<void(bool, const std::vector<ArtworkComment>&, uint64_t)> CommentsCallback;
    void like(const std::string&, uint64_t, const LikeCallback&);
    void unlike(const std::string&, uint64_t, const LikeCallback&);
    void summary(const std::string&, uint64_t optionalUserId, const SummaryCallback&);
    void createComment(const std::string&, uint64_t, const std::string&, const CommentCallback&);
    void listComments(const std::string&, uint64_t beforeId, uint32_t limit, const CommentsCallback&);
};
```

`ArtworkInteractionService` 接受 `const ExhibitionCatalog*`、`SessionService*` 和 DAO；所有写操作先校验作品存在，再验证 User Token。

- [ ] **Step 1: 写 schema 和服务失败测试**

Schema 测试要求：

```bash
grep -Fq 'CREATE TABLE IF NOT EXISTS artwork_likes' sql/jingjie_ar_schema.sql
grep -Fq 'PRIMARY KEY (artwork_id, user_id)' sql/jingjie_ar_schema.sql
grep -Fq 'CREATE TABLE IF NOT EXISTS artwork_comments' sql/jingjie_ar_schema.sql
grep -Fq 'KEY idx_artwork_comments_page (artwork_id, id)' sql/jingjie_ar_schema.sql
grep -Fq 'CREATE TABLE IF NOT EXISTS exhibition_statistics' sql/jingjie_ar_schema.sql
```

服务测试覆盖未知作品 404、匿名点赞 401、重复点赞幂等、评论空白或超过 1000 字节返回 400、`limit` 限制到 20。

- [ ] **Step 2: 运行并确认失败**

Run:

```bash
bash tests/integration/jingjie_ar_schema_static_test.sh
cmake --build build-full --target artwork_interaction_service_test -j2
```

Expected: schema grep 失败，CMake 目标不存在。

- [ ] **Step 3: 增加兼容数据库表**

使用设计稿中的 `exhibition_statistics`、`artwork_likes`、`artwork_comments` 表；`user_id` 类型严格匹配现有 `users.id BIGINT UNSIGNED`，两个作品互动表均增加 `FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE`。不删除或重命名旧场景表。

- [ ] **Step 4: 实现 DAO、服务和 HTTP 处理器**

SQL 必须使用 MySQL 预处理语句：

```sql
INSERT IGNORE INTO artwork_likes (artwork_id, user_id) VALUES (?, ?)
DELETE FROM artwork_likes WHERE artwork_id = ? AND user_id = ?
SELECT COUNT(*), EXISTS(
  SELECT 1 FROM artwork_likes WHERE artwork_id = ? AND user_id = ?
) FROM artwork_likes WHERE artwork_id = ?
SELECT c.id, u.username, c.content
FROM artwork_comments c JOIN users u ON u.id = c.user_id
WHERE c.artwork_id = ? AND c.id < ?
ORDER BY c.id DESC LIMIT ?
```

处理器方法为 `detail`、`like`、`unlike`、`comments`、`comment`，全部返回统一信封。

- [ ] **Step 5: 运行单元和 schema 测试**

Run:

```bash
cmake --build build-full --target artwork_interaction_dao_test \
  artwork_interaction_service_test artwork_interaction_handlers_test -j2
ctest --test-dir build-full -R 'artwork_interaction|schema' --output-on-failure
bash tests/integration/jingjie_ar_schema_static_test.sh
```

Expected: 全部通过。

- [ ] **Step 6: 提交**

```bash
git add sql/jingjie_ar_schema.sql include/db/ArtworkInteractionDAO.h \
  src/db/ArtworkInteractionDAO.cpp WebApps/ARServer/include/services/ArtworkInteractionService.h \
  WebApps/ARServer/src/services/ArtworkInteractionService.cpp \
  WebApps/ARServer/include/handlers/ArtworkInteractionHandlers.h \
  WebApps/ARServer/src/handlers/ArtworkInteractionHandlers.cpp CMakeLists.txt
git add -f tests/unit/artwork_interaction_dao_test.cpp \
  tests/unit/artwork_interaction_service_test.cpp \
  tests/unit/artwork_interaction_handlers_test.cpp \
  tests/integration/jingjie_ar_schema_static_test.sh
git commit -m "实现作品级点赞与评论"
```

---

### Task 5: 将在线状态改为展馆级匿名访客

**Files:**
- Create: `WebApps/ARServer/include/services/VisitorSessionService.h`
- Create: `WebApps/ARServer/src/services/VisitorSessionService.cpp`
- Modify: `WebApps/ARServer/include/services/PresenceService.h`
- Modify: `WebApps/ARServer/src/services/PresenceService.cpp`
- Create: `tests/unit/visitor_session_service_test.cpp`
- Modify: `tests/unit/presence_service_test.cpp`
- Modify: `tests/integration/presence_redis_integration.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
class VisitorStore {
public:
    virtual ~VisitorStore() {}
    virtual bool exists(const std::string& token) = 0;
    virtual bool save(const std::string& token, int ttlSeconds) = 0;
    virtual bool claimBootstrap(const std::string& requestId,
        const std::string& candidateToken, int ttlSeconds,
        std::string* resolvedToken, bool* claimed) = 0;
};
struct VisitorBootstrapResult {
    enum Status { kOk, kBadRequest, kUnavailable };
    Status status;
    std::string token;
    bool incrementView;
};
class VisitorSessionService {
public:
    typedef std::function<std::string()> TokenGenerator;
    VisitorBootstrapResult bootstrap(const std::string& existingToken,
        const std::string& bootstrapRequestId);
    bool valid(const std::string& token) const;
};
class PresenceStore {
public:
    virtual bool touch(const std::string& token, int64_t nowMs) = 0;
    virtual bool remove(const std::string& token) = 0;
    virtual bool count(int64_t cutoffMs, uint64_t* value) = 0;
};
class PresenceService {
public:
    bool heartbeat(const std::string& token, int64_t nowMs);
    bool remove(const std::string& token);
    bool count(int64_t nowMs, uint64_t* value);
};
```

- [ ] **Step 1: 改写失败测试**

断言两个不同 Token 即使浏览不同场景，`count` 仍为 2；重复 heartbeat 不增加人数；超过 60 秒自动清理。Visitor 测试断言有效旧 Token 被复用，新请求 ID 的 `incrementView=true`，相同请求 ID 再次调用为 false。

- [ ] **Step 2: 运行并确认失败**

Run: `cmake --build build-full --target visitor_session_service_test presence_service_test -j2`
Expected: 编译失败，现有 Presence 接口仍要求 `sceneId`。

- [ ] **Step 3: 实现 Redis 键与匿名会话**

固定键：

```text
visitor:{64位十六进制token}
bootstrap:{bootstrapRequestId}
presence:exhibition
```

访客保存使用 `SET key 1 EX 1800`；幂等认领使用 `SET bootstrap:{id} candidateToken NX EX 300`，随后读取该键作为 `resolvedToken`，因此首次响应丢失后的同 ID 重试仍得到同一 Token；在线更新使用 `ZADD presence:exhibition now token`；计数前用 `ZREMRANGEBYSCORE presence:exhibition -inf cutoff`，再 `ZCARD`。

Visitor Token 复用项目已有 `getrandom` 语义，生成 32 字节并编码为 64 位小写十六进制。测试通过注入固定生成器避免依赖随机数。

- [ ] **Step 4: 运行单元和 Redis 集成测试**

Run:

```bash
cmake --build build-full --target visitor_session_service_test \
  presence_service_test presence_redis_integration_test -j2
ctest --test-dir build-full -R 'visitor_session|presence' --output-on-failure
```

Expected: 单元测试通过；Redis 可用时集成测试通过，且只使用 `presence:exhibition`。

- [ ] **Step 5: 提交**

```bash
git add WebApps/ARServer/include/services/VisitorSessionService.h \
  WebApps/ARServer/src/services/VisitorSessionService.cpp \
  WebApps/ARServer/include/services/PresenceService.h \
  WebApps/ARServer/src/services/PresenceService.cpp CMakeLists.txt
git add -f tests/unit/visitor_session_service_test.cpp tests/unit/presence_service_test.cpp \
  tests/integration/presence_redis_integration.cpp
git commit -m "实现展馆级匿名在线统计"
```

---

### Task 6: 实现不去重总浏览量与访客 HTTP 接口

**Files:**
- Create: `include/db/ExhibitionStatisticsDAO.h`
- Create: `src/db/ExhibitionStatisticsDAO.cpp`
- Create: `WebApps/ARServer/include/handlers/VisitorHandlers.h`
- Create: `WebApps/ARServer/src/handlers/VisitorHandlers.cpp`
- Create: `tests/unit/exhibition_statistics_dao_test.cpp`
- Create: `tests/unit/visitor_handlers_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
class ExhibitionStatisticsDAO {
public:
    typedef std::function<void(bool, uint64_t)> CountCallback;
    void incrementAndRead(const std::string& exhibitionId, const CountCallback&);
    void read(const std::string& exhibitionId, const CountCallback&);
};
class VisitorHandlers {
public:
    void bootstrap(const HttpRequest&, const AsyncResponder&) const;
    void heartbeat(const HttpRequest&, const AsyncResponder&) const;
    void exit(const HttpRequest&, const AsyncResponder&) const;
    void presence(const HttpRequest&, const AsyncResponder&) const;
    void views(const HttpRequest&, const AsyncResponder&) const;
};
```

- [ ] **Step 1: 写 DAO 和处理器失败测试**

测试请求必须使用 JSON：

```json
{"bootstrapRequestId":"page-550e8400-e29b-41d4-a716-446655440000"}
```

旧 Visitor Token 通过 `X-Visitor-Token` 传入。成功数据：

```json
{"visitorToken":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","totalViews":1287,"statisticsAvailable":true}
```

同一 `bootstrapRequestId` 第二次调用返回同一 Token，且 Fake DAO 的 increment 次数仍为 1。心跳缺 Token 返回 `401 VISITOR_TOKEN_REQUIRED`，在线人数只返回 `{"onlineCount":2}`。

- [ ] **Step 2: 运行并确认失败**

Run: `cmake --build build-full --target exhibition_statistics_dao_test visitor_handlers_test -j2`
Expected: CMake 目标不存在。

- [ ] **Step 3: 实现总浏览量原子 SQL**

```sql
INSERT INTO exhibition_statistics (exhibition_id, total_views)
VALUES (?, 1)
ON DUPLICATE KEY UPDATE total_views = LAST_INSERT_ID(total_views + 1);
SELECT total_views FROM exhibition_statistics WHERE exhibition_id = ?;
```

两条语句在同一个 DBWorkerPool 任务和同一连接中执行。首次插入和更新分支均返回准确值。

- [ ] **Step 4: 实现访客处理器及降级**

bootstrap 在 `cacheWorkers` 中执行 Redis 会话和幂等认领；只有 `incrementView=true` 时调用 DAO。MySQL 统计失败仍返回 200，`totalViews:null`、`statisticsAvailable:false`；Redis 无法建立匿名会话时返回 503，因为客户端无法维持在线身份。presence 或 heartbeat 失败返回 503，但不影响场景 API。

- [ ] **Step 5: 运行测试**

Run:

```bash
cmake --build build-full --target exhibition_statistics_dao_test visitor_handlers_test -j2
ctest --test-dir build-full -R 'exhibition_statistics|visitor_handlers' --output-on-failure
```

Expected: 2/2 tests passed。

- [ ] **Step 6: 提交**

```bash
git add include/db/ExhibitionStatisticsDAO.h src/db/ExhibitionStatisticsDAO.cpp \
  WebApps/ARServer/include/handlers/VisitorHandlers.h \
  WebApps/ARServer/src/handlers/VisitorHandlers.cpp CMakeLists.txt
git add -f tests/unit/exhibition_statistics_dao_test.cpp tests/unit/visitor_handlers_test.cpp
git commit -m "实现访客会话与展馆浏览统计"
```

---

### Task 7: 装配新后端路由并收敛鉴权边界

**Files:**
- Modify: `WebApps/ARServer/include/ARServer.h`
- Modify: `WebApps/ARServer/src/ARServer.cpp`
- Modify: `WebApps/ARServer/src/main.cpp`
- Modify: `WebApps/ARServer/src/middleware/AuthMiddleware.cpp`
- Modify: `WebApps/ARServer/include/config/AppConfig.h`
- Modify: `WebApps/ARServer/src/config/AppConfig.cpp`
- Modify: `WebApps/ARServer/src/handlers/AuthHandler.cpp`
- Modify: `WebApps/ARServer/src/services/AuthService.cpp`
- Modify: `tests/unit/app_config_test.cpp`
- Modify: `tests/unit/request_middleware_test.cpp`
- Modify: `tests/unit/ar_handlers_test.cpp`
- Create: `tests/integration/museum_api_test.sh`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 2–6 的 catalog、handlers、DAO 和 services。
- Produces: 设计稿规定的最终 `/api/*` 路由。

- [ ] **Step 1: 写路由与鉴权失败测试**

公开：`POST /api/visitors/session`、所有 GET 场景/作品/评论/统计/在线接口。
Visitor：`POST /api/presence/heartbeat`、`POST /api/presence/exit`。
User：作品点赞、取消点赞、发表评论。

测试确保 Visitor Token 不能访问作品写接口，User Token 不自动替代 Visitor Token。公开 GET 请求携带合法 Bearer Token 时，中间件仍应填充可选的 `auth.token`，供作品详情返回当前用户是否已点赞；不携带时不得返回 401。

- [ ] **Step 2: 运行并确认失败**

Run:

```bash
cmake --build build-full --target request_middleware_test ar_handlers_test -j2
ctest --test-dir build-full -R 'request_middleware|ar_handlers' --output-on-failure
```

Expected: 新路由鉴权断言失败。

- [ ] **Step 3: 在 main 中加载配置并装配依赖**

`main` 在创建监听器之前执行：

```cpp
std::vector<std::string> catalogErrors;
std::unique_ptr<ar::ExhibitionCatalog> catalog =
    ar::ExhibitionCatalog::load(config.exhibitionConfig, config.staticRoot, &catalogErrors);
if (!catalog) {
    for (size_t i = 0; i < catalogErrors.size(); ++i)
        std::cerr << "exhibition configuration error: " << catalogErrors[i] << std::endl;
    return 2;
}
```

随后构造 `ArtworkInteractionDAO`、`ExhibitionStatisticsDAO`、`VisitorSessionService`、`PresenceService` 及处理器。任何处理器捕获的 catalog 指针生命周期必须短于 `catalog`。

- [ ] **Step 4: 注册最终路由与 CORS 请求头**

增加 `AR_ALLOWED_ORIGIN` 配置，默认空字符串表示同源页面不发送 CORS 头；仅当该值非空时安装 CorsMiddleware，并只允许这个精确来源。允许请求头增加 `X-Visitor-Token`。`AuthMiddleware::before` 先提取可选 Bearer Token，再判断当前路由是否必须登录；注册设计稿全部路由，移除旧 `/api/session`、`/api/session/enter`、`/api/session/exit`、场景点赞评论和按场景成员路由。旧 MySQL 表保留，旧 Session/场景互动处理器源码暂时保留但不再装配。

认证成功数据改为：

```json
{"success":true,"data":{"isNew":false,"username":"alice","userId":7,"token":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},"message":""}
```

错误统一使用 `makeApiError`。

- [ ] **Step 5: 构建并运行后端测试**

Run:

```bash
cmake --build build-full --target ar_server -j2
ctest --test-dir build-full --output-on-failure
```

Expected: 所有启用测试通过，`ar_server` 成功链接。

- [ ] **Step 6: 运行本机 API 集成测试**

使用独立测试数据库和 Redis 启动服务后：

Run: `BASE_URL=http://127.0.0.1:8080 bash tests/integration/museum_api_test.sh`
Expected: 输出 `PASS: museum API`，覆盖游客初始化、刷新计数、在线人数、登录、作品点赞和评论。

- [ ] **Step 7: 提交**

```bash
git add WebApps/ARServer/include/ARServer.h WebApps/ARServer/src/ARServer.cpp \
  WebApps/ARServer/src/main.cpp WebApps/ARServer/src/middleware/AuthMiddleware.cpp \
  WebApps/ARServer/include/config/AppConfig.h WebApps/ARServer/src/config/AppConfig.cpp \
  WebApps/ARServer/src/handlers/AuthHandler.cpp WebApps/ARServer/src/services/AuthService.cpp \
  CMakeLists.txt
git add -f tests/unit/app_config_test.cpp tests/unit/request_middleware_test.cpp tests/unit/ar_handlers_test.cpp \
  tests/integration/museum_api_test.sh
git commit -m "装配展馆业务接口与双令牌鉴权"
```

---

### Task 8: 实现轻量前端的 API、访客和登录模块

**Files:**
- Create: `WebApps/ARServer/www/js/api-client.js`
- Create: `WebApps/ARServer/www/js/visitor-session.js`
- Create: `WebApps/ARServer/www/js/auth-session.js`
- Create: `tests/frontend/api-client.test.mjs`
- Create: `tests/frontend/visitor-session.test.mjs`
- Create: `tests/frontend/auth-session.test.mjs`

**Interfaces:**
- Produces:

```js
export class ApiError extends Error { constructor(status, code, message, requestId) {} }
export class ApiClient {
  constructor({baseUrl = "", fetchImpl = fetch, storage = sessionStorage}) {}
  request(path, {method = "GET", body, visitor = false, user = false,
                 signal, keepalive = false} = {}) {}
}
export class VisitorSession {
  async bootstrap() {}
  startHeartbeat() {}
  stopHeartbeat({sendExit = true} = {}) {}
}
export class AuthSession {
  async ensureAuthenticated() {}
  async authenticate(username, password) {}
  clear() {}
}
```

- [ ] **Step 1: 写 Node 内置测试**

测试 fake fetch 收到：

```js
assert.equal(options.headers["X-Visitor-Token"], "visitor-1");
assert.equal(options.headers.Authorization, "Bearer user-1");
```

测试 API `success:false` 抛出 `ApiError`；bootstrap 每次生成新的 `crypto.randomUUID()`，存储 Visitor Token；401 只清除 `ar.userToken`，不清除 `ar.visitorToken`。

- [ ] **Step 2: 运行并确认失败**

Run: `node --test tests/frontend/api-client.test.mjs tests/frontend/visitor-session.test.mjs tests/frontend/auth-session.test.mjs`
Expected: FAIL，模块不存在。若开发机未安装 Node.js，先安装 Node.js 18+；生产服务器不需要 Node。

- [ ] **Step 3: 实现三个模块**

存储键固定为：

```js
const VISITOR_TOKEN_KEY = "ar.visitorToken";
const USER_TOKEN_KEY = "ar.userToken";
```

heartbeat 每 30 秒发送一次；`pagehide` 通过 `ApiClient.request("/api/presence/exit", {method:"POST", visitor:true, keepalive:true})` 调用 exit。bootstrap 失败不阻塞后续场景加载，但页面将在线统计显示为“暂不可用”。

- [ ] **Step 4: 运行测试**

Run: `node --test tests/frontend/*.test.mjs`
Expected: 全部通过。

- [ ] **Step 5: 提交**

```bash
git add WebApps/ARServer/www/js/api-client.js \
  WebApps/ARServer/www/js/visitor-session.js WebApps/ARServer/www/js/auth-session.js
git add -f tests/frontend/api-client.test.mjs tests/frontend/visitor-session.test.mjs \
  tests/frontend/auth-session.test.mjs
git commit -m "实现展馆前端会话与接口客户端"
```

---

### Task 9: 实现 krpano 适配器和完整展馆界面

**Files:**
- Replace: `WebApps/ARServer/www/index.html`
- Create: `WebApps/ARServer/www/css/museum.css`
- Create: `WebApps/ARServer/www/js/krpano-adapter.js`
- Create: `WebApps/ARServer/www/js/artwork-modal.js`
- Create: `WebApps/ARServer/www/js/museum-app.js`
- Delete: `WebApps/ARServer/www/css/style.css`
- Delete: `WebApps/ARServer/www/css/panorama-loading.css`
- Delete: `WebApps/ARServer/www/js/app.js`
- Delete: `WebApps/ARServer/www/vendor/aframe-1.6.0.min.js`
- Delete: `WebApps/ARServer/www/assets/panoramas/`
- Delete: `WebApps/ARServer/www/assets/panoramas-preview/`
- Delete: `WebApps/ARServer/www/assets/thumbnail/`
- Create: `tests/frontend/krpano-adapter.test.mjs`
- Create: `tests/integration/museum_frontend_static_test.sh`

**Interfaces:**
- Consumes: Task 8 模块和 `/api/scenes`、`/api/scenes/:id`、作品接口。
- Produces: 不依赖旧平台脚本的完整浏览页面。

- [ ] **Step 1: 写 XML、竞态和静态依赖失败测试**

`krpano-adapter.test.mjs` 断言：

```js
assert.equal(xmlEscape(`<tag a="1">&'`),
  "&lt;tag a=&quot;1&quot;&gt;&amp;&apos;");
assert.match(buildSceneXml(scene), /15949056_%s\.jpg/);
assert.doesNotMatch(buildSceneXml(scene), /type="inactive"/);
```

静态测试要求：

```bash
grep -Fq 'type="module"' WebApps/ARServer/www/index.html
grep -Fq '/assets/krp/runtime/player_krp_v2.js' WebApps/ARServer/www/index.html
! grep -R -E '720yun|api\.map|amap|panoOffline\.js|aframe' \
  WebApps/ARServer/www/index.html WebApps/ARServer/www/js WebApps/ARServer/www/css
```

- [ ] **Step 2: 运行并确认失败**

Run:

```bash
node --test tests/frontend/krpano-adapter.test.mjs
bash tests/integration/museum_frontend_static_test.sh
```

Expected: 模块缺失或旧 A-Frame 引用导致失败。

- [ ] **Step 3: 实现 krpano-adapter**

`embedpano` 只调用一次。适配器提供：

```js
export function xmlEscape(value) {}
export function buildSceneXml(scene) {}
export class KrpanoAdapter {
  constructor({targetId, onHotspot}) {}
  async initialize() {}
  async loadScene(scene, generation) {}
  getView() {}
}
```

XML 必须包含 preview、cube、view 和可渲染热点；所有字符串经过 XML 转义。切换场景时保留切换前的 `hlookat/vlookat/fov`，除非首次进入。`inactive` 不渲染。高清失败时不清除 preview。

- [ ] **Step 4: 实现页面、作品弹窗和状态协调**

页面包含：展馆标题、简介入口、场景目录、全景容器、总浏览量、展馆在线人数、作品弹窗、登录弹窗、非阻塞错误提示。

`museum-app.js` 启动顺序：

```js
await Promise.allSettled([visitor.bootstrap(), loadCounters()]);
const catalog = await api.request("/api/scenes");
await switchScene(catalog.defaultSceneId);
visitor.startHeartbeat();
```

每次 `switchScene` 递增 generation 并取消旧 fetch；只有当前 generation 可以调用播放器。作品正文使用 `textContent`，评论失败保留输入内容；用户登录成功后重试一次待执行的点赞或评论。

- [ ] **Step 5: 运行前端测试并人工检查**

Run:

```bash
node --test tests/frontend/*.test.mjs
bash tests/integration/museum_frontend_static_test.sh
./scripts/provision_krpano.sh pano/html/assets/krp
```

启动服务后在浏览器验证：先显示 preview，随后高清切片清晰；快速切换不回跳；阻止高清请求仍可操作；Network 不出现旧平台请求。

Expected: 自动测试全部通过，人工验收四项均符合。

- [ ] **Step 6: 提交**

```bash
git add WebApps/ARServer/www/index.html WebApps/ARServer/www/css \
  WebApps/ARServer/www/js
git add -u WebApps/ARServer/www
git add -f tests/frontend/krpano-adapter.test.mjs \
  tests/integration/museum_frontend_static_test.sh
git commit -m "接入 krpano 展馆前端界面"
```

---

### Task 10: 完成公开上线安全、全链路验收和部署文档

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `WebApps/ARServer/include/services/AuthService.h`
- Modify: `WebApps/ARServer/src/services/AuthService.cpp`
- Modify: `WebApps/ARServer/include/services/DaoAuthStore.h`
- Modify: `WebApps/ARServer/src/services/DaoAuthStore.cpp`
- Modify: `include/db/SessionDAO.h`
- Modify: `src/db/SessionDAO.cpp`
- Modify: `tests/unit/ar_handlers_test.cpp`
- Create: `tests/integration/museum_end_to_end_test.sh`
- Create: `tests/integration/assets_manifest_test.sh`
- Create: `docs/operations/krpano-museum-deployment.md`

**Interfaces:**
- Produces: Argon2id 新密码哈希、旧 SHA-256 登录后升级、最终发布和回滚步骤。

- [ ] **Step 1: 写密码迁移与全链路失败测试**

认证单元测试断言新密码格式以 `$argon2id$` 开头；旧 `sha256:` 用户仍能登录，并调用一次 `updatePasswordHash(userId, newHash)`。

全链路脚本执行：

1. 游客 A 初始化，浏览量 +1。
2. 相同请求 ID 重试，浏览量不变。
3. 游客 B 初始化，在线人数为 2。
4. A 切换场景，在线人数仍为 2。
5. 未登录作品写接口返回 401。
6. 登录后点赞和评论成功。
7. 同一作品从两个场景热点打开时互动数相同。
8. A 退出后在线人数为 1。

- [ ] **Step 2: 运行并确认失败**

Run:

```bash
cmake --build build-full --target ar_handlers_test -j2
bash tests/integration/assets_manifest_test.sh
BASE_URL=http://127.0.0.1:8080 bash tests/integration/museum_end_to_end_test.sh
```

Expected: 密码格式或新增脚本失败。

- [ ] **Step 3: 接入 Argon2id 并实现登录后升级**

CMake 查找 `libargon2`，生产构建缺失时明确失败。参数固定为 Argon2id v1.3、64 MiB、3 次迭代、并行度 1、16 字节随机盐、32 字节哈希。`AuthStore` 增加：

```cpp
virtual void updatePasswordHash(
    uint64_t userId, const std::string& hash,
    const std::function<void(bool)>& callback) = 0;
```

新用户直接保存 Argon2id；旧用户 SHA-256 验证成功后先升级哈希，再签发 Session。任何日志不得输出密码、哈希或 Token。

- [ ] **Step 4: 增加资源清单与完整测试**

`assets_manifest_test.sh` 重新计算每个资源的大小和 SHA-256，并验证 `exhibition.json` 所有 `/assets/` 引用存在。对 krpano 授权运行文件只验证部署路径存在，不把其哈希写入公开清单。

Run:

```bash
cmake --build build-full --target ar_server -j2
ctest --test-dir build-full --output-on-failure
node --test tests/frontend/*.test.mjs
bash tests/integration/pano_migration_test.sh
bash tests/integration/assets_manifest_test.sh
BASE_URL=http://127.0.0.1:8080 bash tests/integration/museum_end_to_end_test.sh
```

Expected: 所有测试通过。

- [ ] **Step 5: 编写部署和回滚文档**

文档写明：

- 备份数据库、当前二进制、静态目录和配置。
- 安装 `libargon2-dev`、Git LFS，并执行 `git lfs pull`。
- 执行 additive schema、配置 `AR_EXHIBITION_CONFIG`。
- 从私有授权包执行 `provision_krpano.sh`。
- 构建 `ar_server`，回环地址验证后重启 systemd。
- Nginx 对 `/assets/` 长缓存、`index.html` 不缓存、`/api/` 不缓存。
- Nginx 在 `http` 级定义按客户端 IP 的限流区：认证 `5r/m`、访客初始化 `30r/m`、评论 `10r/m`；对应精确 location 分别使用 `burst=5 nodelay`、`burst=10 nodelay`、`burst=10 nodelay`，其他只读 API 不套用写接口限流。
- Nginx 返回适配 krpano 的 CSP：`default-src 'self'; img-src 'self' data: blob:; media-src 'self' blob:; script-src 'self' 'unsafe-eval'; style-src 'self' 'unsafe-inline'; connect-src 'self'; worker-src 'self' blob:`，不得额外放开第三方域名。
- 使用版本软链接切换新前端；失败时切回旧二进制和静态目录，不回滚删除新表。
- 上线后运行 HTTPS API、浏览器和 30 秒并发压测，记录 RPS、P99、超时和非 2xx。

- [ ] **Step 6: 最终提交**

```bash
git add CMakeLists.txt WebApps/ARServer/include/services/AuthService.h \
  WebApps/ARServer/src/services/AuthService.cpp \
  WebApps/ARServer/include/services/DaoAuthStore.h \
  WebApps/ARServer/src/services/DaoAuthStore.cpp \
  include/db/SessionDAO.h src/db/SessionDAO.cpp
git add -f tests/unit/ar_handlers_test.cpp tests/integration/museum_end_to_end_test.sh \
  tests/integration/assets_manifest_test.sh docs/operations/krpano-museum-deployment.md
git commit -m "完成展馆公开上线安全与验收"
```

- [ ] **Step 7: 最终发布前检查**

Run:

```bash
git status --short
git lfs fsck
cmake --build build-full --target ar_server -j2
ctest --test-dir build-full --output-on-failure
```

Expected: 只存在用户明确保留的无关工作区修改；LFS 完整；构建和全部测试通过。不得把 `pano/` 原始交接目录、构建目录、日志、凭据或 krpano 私有运行文件加入提交。
