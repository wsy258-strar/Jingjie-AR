# 境界 AR（Jingjie-AR）

> 基于自研 C++11 HTTP 框架的线上 360° 全景数字展馆。

![境界 AR 展馆截图](img/image.png)

境界 AR 是一个面向线上展览浏览与互动的全栈 C++ 项目。项目以自研的 `http_framework` 为底座，使用 krpano 渲染多场景全景展馆；ARServer 负责展馆目录、匿名访客、在线人数、注册登录、作品点赞和评论等业务，并可通过 Nginx、systemd 部署到 Ubuntu 云服务器。

当前示例展馆为“画叙勤廉·浙江美术馆馆藏作品展”，包含 9 个全景场景和 17 件作品资料。

## 项目亮点

- **自研 C++11 网络框架**：基于 `epoll` 的 Multi-Reactor 架构，主 Reactor 接收连接，子 EventLoop 处理 I/O；包含 `TcpServer`、Buffer、TimerQueue、任务投递与连接生命周期管理。
- **完整 HTTP/1.1 能力**：支持增量解析、Keep-Alive、参数路由、中间件、CORS、异步响应，以及 ETag、Last-Modified、单段 Range（206）和 `sendfile` 静态文件传输。
- **异步业务隔离**：MySQL、Redis 等阻塞 I/O 经连接池、`DBWorkerPool` 与缓存任务池处理，避免占用 EventLoop；密码使用 Argon2id 哈希。
- **展馆业务闭环**：首次访问创建匿名访客会话，用于不去重总浏览量与展馆级在线人数；登录后可对每件作品点赞、评论；游客仍可完整浏览展厅与作品。
- **沉浸式前端**：krpano 全景场景、热点跳转、底部场景选择、全屏/音乐/VR/视角控制；作品查看器支持多图切换、缩放、重置、收藏状态和分享深链。
- **可运维部署**：提供环境变量配置、异步滚动日志、Nginx 反向代理与缓存、systemd 常驻服务、HTTPS、发布校验与回滚文档。

## 架构

```text
浏览器（krpano 展馆页面）
        │ HTTPS / HTTP
        ▼
      Nginx（可选：TLS、缓存、限流、静态资源）
        │ 127.0.0.1:8080
        ▼
      ar_server
  路由 / 中间件 / 异步业务处理器
        │
        ▼
  http_framework
Multi-Reactor / TcpServer / Buffer / TimerQueue
        │                 │
        ▼                 ▼
     MySQL              Redis
用户、会话、互动、统计    Session 缓存、访客身份、在线状态
```

`ar_server` 本身只提供 HTTP；生产环境由 Nginx 终止 TLS，并建议将服务监听在 `127.0.0.1:8080`。

## 功能与接口

| 业务 | 能力 | 接口 |
| --- | --- | --- |
| 展馆目录 | 返回展馆信息、场景、全景资源、热点与作品映射 | `GET /api/scenes` |
| 场景详情 | 返回单个场景的完整配置 | `GET /api/scenes/:sceneId` |
| 匿名访客 | 创建/恢复访客会话，记录访问与在线状态 | `POST /api/visitors/session` |
| 展馆在线 | 心跳、退出和获取展馆当前在线人数 | `POST /api/presence/heartbeat`、`POST /api/presence/exit`、`GET /api/presence` |
| 浏览统计 | 获取展馆总浏览量（不去重） | `GET /api/statistics/views` |
| 注册与登录 | 注册或登录、退出登录 | `POST /api/auth`、`POST /api/auth/logout` |
| 作品详情 | 返回作品资料、点赞状态、点赞数和评论总数 | `GET /api/artworks/:artworkId` |
| 作品互动 | 点赞/取消点赞、分页读取/发布评论 | `POST` / `DELETE /api/artworks/:artworkId/likes`；`GET` / `POST /api/artworks/:artworkId/comments` |

- 游客默认可进入展馆、切换场景和查看作品；点赞、评论需要登录。
- 在线人数是**当前展馆**内保持心跳的匿名访客与登录用户总数；总浏览量按访问累计，不去重。
- 收藏仅保存在浏览器本地；分享生成包含 `?artwork=<id>` 的作品深链接。
- 背景音乐在相同音乐资源的场景间保持播放进度。浏览器对首次有声自动播放有策略限制，首次交互后可恢复播放。

## 仓库结构

```text
.
├── include/                         # http_framework 对外头文件
├── src/                             # Reactor、HTTP、路由、中间件、缓存与日志实现
├── memory/                          # 固定槽位内存池
├── WebApps/ARServer/
│   ├── config/exhibition.json       # 展馆、场景、热点、作品目录
│   ├── include/、src/                # ARServer 业务层
│   └── www/                          # krpano 页面、全景、作品与音频资源
├── sql/jingjie_ar_schema.sql        # 可重复执行的建表迁移
├── scripts/                         # krpano 资源安装与迁移脚本
├── benchmark/                       # 压测脚本与原始结果
└── docs/operations/                 # 生产部署、验收与回滚说明
```

## 本地运行

### 1. 安装依赖

以 Ubuntu/Debian 为例：

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config git git-lfs \
  libargon2-dev libmysqlclient-dev libhiredis-dev mysql-client redis-server
git lfs install
git lfs pull
```

启动 MySQL 与 Redis，并创建仅供应用使用的数据库和最小权限用户。若要运行 MySQL 集成测试，还需要准备独立的测试数据库。

### 2. 配置环境并初始化数据库

```bash
cp WebApps/ARServer/.env.example .env.arserver
# 编辑 .env.arserver：至少设置 MYSQL_PASSWORD；生产环境还应设置 AR_HOST=127.0.0.1。

set -a
.env.arserver
set +a
MYSQL_PWD="$MYSQL_PASSWORD" mysql -h "$MYSQL_HOST" -P "$MYSQL_PORT" \
  -u "$MYSQL_USER" "$MYSQL_DATABASE" < sql/jingjie_ar_schema.sql
```

重要配置项：

- `AR_STATIC_ROOT`：静态站点根目录，默认 `WebApps/ARServer/www`。
- `AR_EXHIBITION_CONFIG`：展馆配置文件，默认 `WebApps/ARServer/config/exhibition.json`。
- `AR_ALLOWED_ORIGIN`：允许的单一前端 Origin；生产环境应设置为 `https://你的域名`。
- `AR_LOG_ENABLED`：默认写入 `logs/`；设置为 `false` 时只输出到终端。
- `AR_LOG_ROLL_SIZE_MB`、`AR_LOG_RETENTION_DAYS`：日志滚动大小与保留天数。

不要提交 `.env.arserver`，也不要在命令行历史、截图或日志中泄露密码和 Token。

### 3. 准备 krpano 运行资源并构建

krpano 的授权运行文件不纳入仓库。获得合法运行包后执行：

```bash
./scripts/provision_krpano.sh /你的/krpano/html/assets/krp

cmake -S . -B build-full -DCMAKE_BUILD_TYPE=Debug
cmake --build build-full --target ar_server -j2
```

若 CMake 提示缺少 Argon2，请安装 `libargon2-dev`；不要以不安全哈希替代。

### 4. 启动与访问

```bash
set -a
.env.arserver
set +a
./build-full/bin/ar_server
```

浏览器访问 `http://127.0.0.1:8080/`。启动后可用以下命令检查服务：

```bash
curl -fsS http://127.0.0.1:8080/api/scenes
curl -fsS http://127.0.0.1:8080/api/statistics/views
```

## 验证与压测

```bash
# C++ 静态文件 Range/缓存回归
cmake --build build-full --target static_file_handler_test -j2
./build-full/bin/static_file_handler_test

# 前端模块与静态接线验证（需要 Node.js）
node --test tests/frontend/*.test.mjs
bash tests/integration/museum_frontend_static_test.sh

# 仅压测公开只读接口；请在独立压测机记录 RPS、P99、错误和超时。
wrk -t2 -c100 -d30s --latency https://你的域名/api/scenes
```

MySQL/Redis 集成测试会写入测试数据，应使用隔离数据库和缓存实例，避免对生产环境直接执行。

## 技术栈

`C++11`、`epoll`、Multi-Reactor、HTTP/1.1、MySQL、Redis/hiredis、Argon2id、CMake、krpano、Nginx、systemd、Let's Encrypt、Git LFS。

## 许可证与素材

项目使用 [MIT License](LICENSE)。krpano 运行文件、全景图、作品图片和音频仅可在已获得相应授权的范围内使用和分发。
