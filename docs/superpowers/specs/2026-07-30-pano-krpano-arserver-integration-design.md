# krpano 展馆前后端一体化改造设计

日期：2026-07-30
修订：2026-07-31
状态：已按审阅意见修订，待最终确认

## 1. 背景

项目当前由两部分组成：

- `ARServer`：基于自研 C++11 HTTP 框架实现的后端服务，已经具备用户认证、场景目录、在线状态、点赞评论、静态资源响应等能力。
- `pano/`：由第三方全景平台导出的静态展馆，包含 krpano 播放器、9 个场景、热点、全景切片、预览图、缩略图、作品图片和背景音乐，但页面依赖体积较大的旧平台业务脚本及旧接口。

现有 `WebApps/ARServer/www` 中的 A-Frame 页面不再作为正式前端。目标是在保留已授权 krpano 播放能力和全景素材的前提下，重写轻量、可维护的展馆前端壳，并将其接入 ARServer，形成可以独立部署和持续迭代的完整展馆系统。

## 2. 目标与范围

### 2.1 目标

1. 游客无需注册即可打开展馆、切换场景并浏览作品。
2. 首次打开或刷新页面时记录一次不去重的展馆总浏览量。
3. 使用匿名访客会话维护整个展馆的在线人数，不按场景拆分，也不公开访客身份。
4. 点赞和评论以“作品”为归属维度，操作前要求用户登录。
5. 保留 krpano 播放器和已有素材，移除对原第三方全景平台接口及业务脚本的运行时依赖。
6. 场景、作品、热点和素材信息由独立 JSON 配置维护，C++ 后端作为配置的唯一对外数据入口。
7. 保持 Nginx、ARServer、MySQL、Redis、systemd 的现有生产部署体系。

### 2.2 本期不包含

- 展馆内容管理后台和可视化热点编辑器。
- 多展馆租户体系、订单、票务及支付功能。
- 将 krpano 替换为 A-Frame、Three.js 或其他播放器。
- 基于用户、IP 或设备指纹的浏览量去重。
- 删除旧 `scene_likes`、`scene_comments` 表及其历史数据。

## 3. 核心产品规则

### 3.1 三个统计维度

| 对象 | 功能 | 身份要求 | 存储位置 |
|---|---|---|---|
| 整个展馆 | 总浏览量 | 无需登录，每次完整初始化计数一次 | MySQL |
| 整个展馆 | 当前在线人数 | 匿名 Visitor Token | Redis |
| 单个作品 | 点赞、评论 | 注册或登录后的 User Token | MySQL |

作品热点只是作品在场景中的展示入口。多个热点可以引用同一个 `artworkId`，其点赞数和评论数据仍归属于同一作品。

### 3.2 双 Token 模型

- `Visitor Token` 用于匿名会话和展馆在线心跳，通过 `X-Visitor-Token` 请求头传递。
- `User Token` 用于需要登录的点赞和评论，通过 `Authorization: Bearer <token>` 传递。
- 两种 Token 均保存到浏览器 `sessionStorage`，不放入 URL。
- 用户登录后继续沿用原 Visitor Token，避免在线统计出现重复身份。
- Visitor Token 不能替代 User Token，匿名访客无法调用受保护的作品写接口。

## 4. 总体架构

```text
浏览器
├── 轻量展馆前端壳
├── krpano 播放器
├── Visitor Token
└── 可选 User Token
        │
        ▼
Nginx
├── /assets/* ───────────────► 直接返回全景、图片、音乐等静态资源
├── /api/* ──────────────────► ARServer
└── 其他页面资源 ─────────────► 新展馆前端
                                   │
                    ┌──────────────┼──────────────┐
                    ▼              ▼              ▼
              展馆 JSON 配置      Redis          MySQL
              场景/作品/热点      访客/在线       用户/互动/统计
```

生产静态根目录继续使用 `WebApps/ARServer/www`。`pano/` 仅作为原始交接资料和迁移输入，不直接作为线上站点根目录。

## 5. 前端设计

### 5.1 技术选择与目录

前端使用原生 HTML、CSS 和 ES Modules，不引入 React、Vue，也不依赖旧平台压缩业务包。建议结构如下：

```text
WebApps/ARServer/www/
├── index.html
├── css/
│   └── museum.css
├── js/
│   ├── api-client.js
│   ├── visitor-session.js
│   ├── auth-session.js
│   ├── krpano-adapter.js
│   ├── artwork-modal.js
│   └── museum-app.js
└── assets/
    ├── krp/
    ├── pano/
    ├── illustration/
    ├── hotspot/
    └── music/
```

职责边界：

- `api-client.js`：统一请求头、响应结构、超时、错误转换和请求取消。
- `visitor-session.js`：创建或恢复匿名会话，发送 heartbeat 和 exit。
- `auth-session.js`：保存用户令牌、调起登录界面、处理 401。
- `krpano-adapter.js`：封装播放器初始化、场景 XML 生成、热点创建及事件桥接。
- `artwork-modal.js`：显示作品介绍、图集、点赞和评论。
- `museum-app.js`：页面状态协调，不直接操作 krpano 内部细节。

### 5.2 页面启动流程

```text
加载 index.html
  → POST /api/visitors/session
  → 保存 Visitor Token，并使展馆总浏览量 +1
  → GET /api/scenes
  → GET /api/scenes/{defaultSceneId}
  → 初始化 krpano 并先加载预览图
  → 加载立方体高清切片和热点
  → 每 30 秒 POST /api/presence/heartbeat
```

用户点击作品点赞或提交评论时，如果没有 User Token，则显示登录弹窗。登录成功后重试尚未完成的操作。User Token 失效时清除本地令牌、保留评论输入并重新显示登录弹窗。

### 5.3 krpano 适配

- 页面生命周期内只初始化一次播放器。
- `krpano-adapter.js` 根据后端场景数据生成最小化 XML，完成 preview、cube、初始视角和热点配置。
- 热点点击事件转换为应用层事件，由前端根据明确的热点类型处理。
- `scene` 热点切换场景；`artwork` 热点打开作品详情；其他热点类型分别负责图集、文字、音频、视频或外链。
- 场景切换使用请求序号或 `AbortController`，防止旧请求晚到后覆盖当前场景。
- 高清切片加载失败时保留低清预览，不能让画面变为空白。
- 切换清晰度时保持当前观察方向和缩放比例。

### 5.4 前端安全

- 所有用户生成内容通过 `textContent` 渲染，不拼接到 `innerHTML`。
- 写入 krpano XML 的文字必须进行 XML 转义。
- 静态资源只接受站内 `/assets/` 路径，外链热点只允许 `http` 和 `https` 协议。
- 页面不再请求旧第三方平台接口、高德地图接口及不需要的广告、推广或登录服务。
- 正式环境采用同源 API，并配置适合新页面的 Content Security Policy。

## 6. 展馆配置模型

配置文件位置：

```text
WebApps/ARServer/config/exhibition.json
```

生产环境可通过 `AR_EXHIBITION_CONFIG` 指定绝对路径。`pano/html/index.html` 是本期展馆业务内容的唯一来源，`exhibition.json` 只对原数据做结构规范化和部署路径转换，不新增、改写或推测原页面没有的标题、作者、年份、分类、说明、热点或媒体内容。

以下为省略部分长正文和配置项的结构摘录，展示出的值均来自 `pano/html/index.html`；实际生成文件必须包含原页面对应的完整正文和配置：

```json
{
  "exhibition": {
    "id": "19491365",
    "title": "画叙勤廉·浙江美术馆馆藏作品展",
    "defaultSceneId": "76196992"
  },
  "artworks": [
    {
      "artworkId": "s_76196995_2",
      "title": "《启航》",
      "images": ["/assets/illustration/qihang.jpg"]
    }
  ],
  "scenes": [
    {
      "sceneId": "76196992",
      "panoId": "15949056",
      "name": "展厅入口",
      "previewUrl": "/assets/pano/15949056/preview.jpg",
      "cubeUrl": "/assets/pano/15949056/%s.jpg",
      "hotspots": [
        {
          "hotspotId": "s_76196992_0",
          "type": "scene",
          "title": "进入展厅",
          "ath": -4.29546511,
          "atv": 13.90596409,
          "targetPanoId": "15949055",
          "iconUrl": "/assets/hotspot/new_spotd1_gif.png"
        }
      ]
    }
  ]
}
```

`ExhibitionCatalog` 在服务启动时完整读取并校验配置，成功后作为不可变数据供多个工作线程共享。以下错误直接导致服务启动失败并打印明确日志：

- 展馆、场景、作品或热点 ID 重复。
- 默认场景不存在。
- 场景跳转目标或作品引用不存在。
- 视角、热点坐标越界或字段类型错误。
- 不同热点类型缺少对应必填字段。
- 资源路径不是 `/assets/` 下的站内路径。
- 全景切片、预览图或必要素材不存在。
- 规范化结果缺少原页面中已有的场景、热点、正文、图片、音乐或相关配置。

原字段的迁移规则固定记录在转换工具及测试中：数字和字符串表示差异可以按目标类型规范化；旧资源路径可以统一增加 `/assets` 前缀；除此之外不允许修改业务值。原页面缺少的可选字段在 JSON 中省略，不使用占位内容补齐。无法可靠识别语义的数据保留原始字段并报告迁移错误，不通过人工猜测生成新内容。

嵌套 JSON 解析采用项目固定版本的 `nlohmann/json` 单头文件依赖，版本随源码锁定，避免云服务器与本地使用不同系统包。

## 7. 后端模块设计

| 模块 | 职责 |
|---|---|
| `ExhibitionCatalog` | 加载、校验并查询展馆、场景、热点和作品配置 |
| `SceneHandler` | 返回场景列表与场景详情 |
| `ArtworkHandler` | 返回作品详情、点赞信息和评论列表 |
| `VisitorSessionService` | 生成安全随机 Visitor Token、恢复会话、触发浏览量记录 |
| `PresenceService` | 维护整个展馆的访客心跳与在线人数 |
| `ArtworkInteractionService` | 校验作品、用户和内容，编排点赞评论操作 |
| `ArtworkInteractionDAO` | 访问作品点赞和评论表 |
| `StatisticsDAO` | 原子增加并读取展馆总浏览量 |
| `AuthMiddleware` | 验证 User Token，保护作品写接口 |

MySQL 操作继续交给现有 `DBWorkerPool`，不得阻塞 EventLoop。Redis 不可用、统计写入失败或在线接口异常时，记录错误但不阻断全景浏览。

## 8. API 设计

### 8.1 统一响应

成功响应：

```json
{
  "success": true,
  "data": {},
  "message": ""
}
```

失败响应：

```json
{
  "success": false,
  "data": null,
  "message": "面向用户的错误说明",
  "code": "STABLE_ERROR_CODE",
  "requestId": "请求追踪标识"
}
```

HTTP 状态码表达协议语义，`code` 用于前端稳定判断，不能只依靠中文错误信息。

### 8.2 路由

| 方法 | 路径 | 鉴权 | 说明 |
|---|---|---|---|
| `POST` | `/api/visitors/session` | 无 | 创建或恢复匿名会话，并记录一次总浏览 |
| `GET` | `/api/scenes` | 无 | 获取场景摘要列表和默认场景 |
| `GET` | `/api/scenes/:sceneId` | 无 | 获取单个场景、视角和热点配置 |
| `POST` | `/api/presence/heartbeat` | Visitor | 刷新当前访客的展馆在线状态 |
| `POST` | `/api/presence/exit` | Visitor | 主动退出展馆在线集合 |
| `GET` | `/api/presence` | 无 | 返回整个展馆当前在线人数 |
| `GET` | `/api/statistics/views` | 无 | 查询展馆总浏览量 |
| `GET` | `/api/artworks/:artworkId` | 可选 User | 获取作品详情、点赞数及当前用户状态 |
| `POST` | `/api/artworks/:artworkId/likes` | User | 点赞作品 |
| `DELETE` | `/api/artworks/:artworkId/likes` | User | 取消点赞 |
| `GET` | `/api/artworks/:artworkId/comments` | 无 | 分页获取作品评论 |
| `POST` | `/api/artworks/:artworkId/comments` | User | 发表评论 |
| `POST` | `/api/auth` | 无 | 注册或登录，返回 User Token |

新的前端不再将 Token 或场景 ID 放入查询字符串。旧接口兼容逻辑仅在迁移测试期间保留，旧页面退役并验证新页面稳定后再单独清理。

## 9. 匿名会话与在线统计

### 9.1 Redis 数据

```text
visitor:{token}         Hash/String，匿名会话，TTL 30 分钟
presence:exhibition     Sorted Set，member 为 token，score 为最近心跳时间
```

- Visitor Token 使用安全随机数生成，不能使用递增 ID 或可预测值。
- 心跳间隔为 30 秒，在线窗口为 60 秒。
- 查询人数前通过 `ZREMRANGEBYSCORE` 清理超时成员，再读取 `ZCARD`。
- 创建或恢复匿名会话时将 Visitor Token 写入展馆在线集合。
- heartbeat 必须验证 Visitor Token 仍有效，再刷新其在展馆在线集合中的时间戳。
- 场景切换不修改在线集合，也不改变展馆在线人数。
- exit 使用 `ZREM` 主动移除 Visitor Token。
- 页面退出使用带 `keepalive` 的请求；即使退出请求丢失，成员也会在在线窗口过期后自动移除。

### 9.2 浏览量语义

`POST /api/visitors/session` 每成功完成一次页面初始化就执行一次 MySQL 原子递增，即使请求带有仍然有效的 Visitor Token 也不去重。前端应用内部的场景切换和心跳不增加总浏览量。

同一页面初始化期间若网络重试，前端必须携带本次初始化生成的 `bootstrapRequestId`。服务端短时间保存该请求 ID 的处理结果，防止传输层自动重试误计两次；用户主动刷新会产生新的请求 ID，因此仍按要求增加浏览量。

## 10. 数据库设计

新增表采用 `utf8mb4`，迁移只新增结构，不删除旧表：

```sql
CREATE TABLE exhibition_statistics (
    exhibition_id VARCHAR(64) NOT NULL,
    total_views BIGINT UNSIGNED NOT NULL DEFAULT 0,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (exhibition_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE artwork_likes (
    artwork_id VARCHAR(64) NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (artwork_id, user_id),
    KEY idx_artwork_likes_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE artwork_comments (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    artwork_id VARCHAR(64) NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    content VARCHAR(1000) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    KEY idx_artwork_comments_page (artwork_id, id),
    KEY idx_artwork_comments_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

实现时应根据现有 `users.id` 的实际类型保持 `user_id` 完全一致，并补充相应外键策略。作品 ID 的合法性由 `ExhibitionCatalog` 校验，因为作品内容以配置文件为数据源，不建立重复的作品基础信息表。

总浏览量使用以下形式的原子语句，避免并发下丢失更新：

```sql
INSERT INTO exhibition_statistics (exhibition_id, total_views)
VALUES (?, 1)
ON DUPLICATE KEY UPDATE total_views = total_views + 1;
```

点赞依靠复合主键保证幂等，评论采用基于 `id` 的游标分页，避免数据增长后大偏移量分页退化。

## 11. 原始数据与资源迁移

编写一次性、可重复执行的迁移工具，从 `pano/html/index.html` 的内嵌状态中提取数据，并生成规范化配置及资源清单。迁移至少覆盖：

- 9 个场景。
- 54 张立方体面图片。
- 9 张预览图和 9 张缩略图。
- 现有热点、作品说明、作品图片和音乐。

`exhibition.json` 必须由 `pano/html/index.html` 确定性生成。原数据中的数字热点类型通过固定映射转换为明确类型；如果某个类型或载荷无法可靠识别，迁移立即失败并报告原热点 ID，不允许猜测其含义或补写内容。人工复核只用于确认转换结果与原页面一致，不用于改写原内容。产物校验应检查：

- 六个立方体面是否齐全。
- 每个静态文件是否存在，并生成大小与校验和清单。
- 每个场景跳转和作品引用是否有效。
- 所有文件名是否适合 URL，是否存在大小写冲突。
- 线上页面是否仍存在对旧平台域名的请求。

大体积全景文件不应直接以普通 Git Blob 长期积累。第一阶段根据代码托管平台能力使用 Git LFS 或版本化部署资源包；krpano 授权文件不得提交到不允许分发的公开仓库。部署资源包保留版本号和校验和，以支持精确回滚。

## 12. 故障降级

- 展馆目录加载失败：显示明确错误和重试按钮，不初始化空播放器。
- 场景加载失败：保留当前场景，不显示半完成的新场景。
- 高清全景加载失败：继续显示低清预览。
- 在线人数、心跳或总浏览统计失败：记录日志，不阻止用户浏览。
- 评论提交失败：保留输入内容，允许用户重试。
- User Token 失效：只清除 User Token，不影响 Visitor Token 和当前全景。
- Redis 会话丢失：前端重新创建匿名会话并恢复当前场景。

## 13. 安全与公开上线要求

1. 对登录、匿名会话创建和评论接口配置合理的频率限制，可由 Nginx 与应用共同承担。
2. 评论长度、编码和空白内容必须在服务端校验，数据库操作必须继续使用参数化语句。
3. 若现有密码仍使用普通 SHA-256，公开注册前升级为 Argon2id 或 bcrypt。旧哈希可在用户成功登录后自动重算为新格式，实现平滑迁移。
4. 正式环境只接受 HTTPS，同源部署并限制 CORS，不向浏览器暴露 Redis、MySQL 或 ARServer 内部地址。
5. 展馆在线接口只返回数量，不返回匿名 Token、IP、所在场景或其他访客标识。
6. 对全景、作品图片、音乐和 krpano 播放器保留版权及授权记录。

## 14. 测试与验收

### 14.1 自动化测试

- 配置单元测试：正常配置、重复 ID、错误引用、坐标越界和非法路径。
- API 测试：响应信封、HTTP 状态、鉴权边界、参数校验和分页。
- Redis 集成测试：进入展馆、重复心跳、跨场景切换、主动退出和自然过期。
- MySQL 集成测试：浏览量并发递增、点赞幂等、取消点赞和评论游标分页。
- 静态资源清单测试：配置引用文件全部存在，立方体六面齐全。
- 浏览器端测试：游客浏览、登录后互动、快速切换、高清失败降级和登录过期。

### 14.2 必须通过的验收场景

1. 清除浏览器会话后可直接进入默认场景，无登录拦截。
2. 连续刷新三次，总浏览量准确增加三次。
3. 两个浏览器进入同一或不同场景时，展馆在线人数均为二；任一浏览器切换场景后在线人数保持不变。
4. 未登录点赞或评论时出现登录弹窗，登录后自动继续原操作。
5. 同一用户重复点赞不会产生重复数据。
6. 同一作品在不同场景出现时共享点赞和评论。
7. 阻止高清图片请求后仍可使用预览图浏览。
8. 快速连续切换场景时不会回跳到旧场景。
9. Network 面板中不存在对原第三方全景平台业务接口的请求。
10. `/assets/` 由 Nginx 直接响应，`/api/` 由 ARServer 返回。
11. `exhibition.json` 中的场景、热点、作品正文、图片和音乐可逐项追溯到 `pano/html/index.html`，且不存在原页面没有的业务内容。

完成正确性测试后，再分别对 ARServer 直连和 Nginx HTTPS 完整链路执行压测；压测结果必须同时报告并发数、RPS、P99、超时和非 2xx 数量。

## 15. 部署、缓存与回滚

### 15.1 发布顺序

1. 备份数据库、当前程序、展馆配置和静态目录。
2. 执行只新增表的数据库迁移。
3. 上传带版本号及校验和的全景资源和 krpano 授权运行文件。
4. 部署新的展馆配置与前端静态文件。
5. 编译 ARServer，先在服务器回环地址验证接口。
6. 重启 systemd 服务并检查日志、健康状态和关键 API。
7. 通过软链接或 Nginx 根目录切换新前端版本。
8. 执行浏览器验收、接口检查和短时压力测试。

### 15.2 Nginx 缓存策略

- `/assets/` 下带版本文件名的全景、图片和音乐设置七天或更长的不可变缓存。
- `index.html` 不缓存或仅短时间缓存，确保前端入口可以及时更新。
- JS、CSS 使用带内容哈希或版本号的文件名后设置长期缓存。
- `/api/` 不使用静态缓存。
- 替换素材时必须使用新文件名并同步修改配置，不能原地覆盖仍在缓存期内的文件。

### 15.3 回滚

本次数据库变更采用只新增、不删除的兼容迁移。出现故障时切回上一版本程序、静态目录软链接和展馆配置即可；旧表及旧数据在本期不删除。数据库结构的最终清理应在新版本稳定运行并完成独立备份后另行设计。

## 16. 实施阶段建议

后续实施计划按以下顺序拆分，以便每个阶段都可独立验证：

1. 固化 JSON 数据模型，完成原始数据与素材迁移工具。
2. 实现 `ExhibitionCatalog` 及场景、作品只读 API。
3. 实现匿名访客、总浏览量和 Redis 在线人数。
4. 将点赞评论由场景维度扩展为作品维度。
5. 重写轻量前端壳并完成 krpano 适配。
6. 补齐自动化测试、安全限制和故障降级。
7. 完成 Nginx、systemd 部署切换及生产验收。
