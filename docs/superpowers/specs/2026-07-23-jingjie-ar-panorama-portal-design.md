# 境界AR：360° 全景场景门户设计

**日期：** 2026-07-23  
**状态：** 已确认，待实施

## 1. 目标与范围

将现有 ARServer 演示页升级为“境界AR”全景场景门户。用户可浏览八个 360° 全景场景；注册/登录用户可点赞、取消点赞和发布评论。场景页显示已登录在线人数、全屏、音乐控制入口、互动按钮和退出按钮。

第一期不包含摄像头、Marker 识别、WebSocket、场景热点、评论回复/删除/举报/审核后台、匿名访客在线统计和音乐素材。

## 2. 已确认的产品决策

- 使用 **A-Frame** 的 `a-scene` 与 `a-sky` 渲染 equirectangular WebP 全景图。
- 不申请摄像头权限；手机支持触控拖动和陀螺仪，电脑支持鼠标拖动。
- AR.js 不作为纯全景的运行时依赖；保留日后接入 Marker/摄像头 AR 的扩展空间。
- 游客可浏览全部场景；游客不能点赞或评论，点击互动按钮提示“请先登录后再进行点赞/评论”。
- 在线人数仅统计已登录且正在发送心跳的用户，不统计匿名访客。
- 点赞可取消；评论仅可发布和浏览。
- 音乐接口预留，但第一期没有音乐文件；未来 `music_url` 非空时进入场景默认尝试播放。

## 3. 首页

### 3.1 顶栏

- 顶栏中间显示：`境界AR · 360° 全景探索`。
- 顶栏右侧：未登录显示“登录 / 注册”；已登录显示用户名和退出登录。
- 登录弹窗包含用户名、密码和提交按钮。用户名不存在时沿用现有语义自动注册；用户名存在时完成登录。

### 3.2 场景卡片

首页展示八张仅含封面图和名称的卡片。桌面端采用四列网格，手机端固定两列。

| 场景 ID | 名称 | WebP 文件 |
|---|---|---|
| `docklands` | 码头区 | `docklands_02_8k.webp` |
| `golden-bay` | 黄金海湾 | `golden_bay_8k.webp` |
| `graaff-reinet-cathedral` | 格拉夫-里内特大教堂 | `graaff_reinet_groote_kerk_8k.webp` |
| `illovo-beach` | 伊洛沃海滩 | `illovo_beach_balcony_8k.webp` |
| `little-paris` | 小巴黎埃菲尔铁塔 | `little_paris_eiffel_tower_8k.webp` |
| `san-giuseppe-bridge` | 圣朱塞佩桥 | `san_giuseppe_bridge_16k.webp` |
| `venetian-crossroads` | 威尼斯十字路口 | `venetian_crossroads_16k.webp` |
| `vignaioli` | 维尼亚约利 | `vignaioli_16k.webp` |

素材复制或映射为静态路径 `/assets/panoramas/<file>`；源目录 `media/8k16k_to_webp` 不由应用运行时修改。

## 4. 场景页

场景页为单页全屏全景视图，A-Frame 场景作为底层，HTML 控制层置于其上。

- **右上：** `在线 N 人`，每 10 秒轮询成员接口。登录用户每 10 秒发送心跳；离开或 30 秒未心跳即不再计数。
- **右侧上部（顶栏下）：** 全屏按钮、音乐按钮。`music_url` 为空时音乐按钮禁用并显示“音乐未配置”；未来有 URL 时用隐藏的 `<audio>` 元素播放/暂停。进入场景时默认调用 `audio.play()`；若浏览器拦截有声自动播放，按钮改为“播放音乐”，由用户点击后开始。
- **右下：** 点赞按钮和点赞总数、评论按钮。未登录时点击只显示登录提示。
- **评论抽屉：** 从底部滑出，列表只显示用户名和评论内容；底部输入框用于发布评论。首屏加载 20 条，向上滚动按游标加载更早评论。
- **左下：** “退出场景”。停止音乐、停止心跳、销毁 A-Frame 场景、调用现有退出接口（若有登录 Session）并回到首页。浏览器返回键执行相同清理。

## 5. 服务端与数据设计

继续使用现有 HTTP Framework、`AuthMiddleware`、`SessionService`、`PresenceService`、MySQL 连接池和 `DBWorkerPool`。数据库读写必须异步投递到工作线程；EventLoop 不等待 MySQL、Redis 或 future。

### 5.1 场景元数据

首版使用 C++ 场景目录（不可变的八条记录），而不是新增场景管理后台。每项包含：ID、中文名、全景静态 URL、可空 `music_url`。此目录是列表与详情接口的唯一来源。

### 5.2 持久化表

```sql
CREATE TABLE scene_likes (
  scene_id VARCHAR(64) NOT NULL,
  user_id BIGINT UNSIGNED NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (scene_id, user_id),
  INDEX idx_scene_likes_scene (scene_id)
);

CREATE TABLE scene_comments (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  scene_id VARCHAR(64) NOT NULL,
  user_id BIGINT UNSIGNED NOT NULL,
  content VARCHAR(300) NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  INDEX idx_scene_comments_cursor (scene_id, id)
);
```

查询评论时与现有用户表连接取得用户名，但响应中只返回用户名、内容和分页游标，不返回密码、Token、邮箱或时间。

### 5.3 API

| 方法与路径 | 登录要求 | 行为 |
|---|---:|---|
| `GET /api/scenes` | 否 | 返回八个场景的卡片元数据。 |
| `GET /api/scenes/:sceneId` | 否 | 返回场景详情：名称、`panorama_url`、`music_url`、点赞总数。 |
| `GET /api/scenes/:sceneId/comments?limit=20&before=<id>` | 否 | 返回评论（用户名、内容）和下一游标。 |
| `POST /api/scenes/:sceneId/likes` | 是 | 幂等点赞；唯一键保证每用户每场景仅一次。 |
| `DELETE /api/scenes/:sceneId/likes` | 是 | 取消当前用户的点赞。 |
| `POST /api/scenes/:sceneId/comments` | 是 | JSON body：`{ "content": "..." }`。 |
| `GET /api/scenes/:sceneId/members` | 否 | 只返回登录活跃用户总数。 |

新写接口使用 `Authorization: Bearer <session-token>`。认证接口改为优先接受 JSON body，旧的 `/api/auth?username=...&password=...` 继续兼容，避免破坏既有四个 API 和前端。

## 6. 安全与错误处理

- 密码仅从 POST body 接收；访问日志和错误日志不记录 password。
- 评论长度为 1--300 字符；输出到前端时只用 `textContent`，不插入 HTML。
- 未认证写入返回 `401`；未知场景或参数错误返回 `400/404`；关键 MySQL 写入失败返回 `503`。
- 素材加载失败显示明确的场景错误态；音乐未配置不创建播放请求；自动播放被浏览器拦截时保留手动播放入口。
- 登录失效时清理前端 Token，恢复为游客态并提示重新登录。

## 7. 验收与测试

- 8 个场景卡片、名称和素材映射正确；手机宽度下保持两列。
- 游客可进入、拖动、陀螺仪浏览与全屏，但点赞/评论均只提示登录。
- 登录后点赞总数正确，重复点赞不重复累加；取消点赞正确减一。
- 登录后评论持久化，刷新/重新进入仍可读取；XSS 文本按纯文本显示。
- 两位登录用户进入同一场景后在线人数正确变化；退出或心跳超时后自动减少。
- 无音乐 URL 时按钮显示“音乐未配置”；设置 URL 后进入场景默认尝试播放，若被浏览器策略拦截则点击按钮可播放。
- 新增 DAO、服务、处理器、API 和前端状态均有单元或集成测试；现有 HTTP Framework 与 ARServer 回归测试保持通过。
