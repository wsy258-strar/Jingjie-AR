# krpano 展馆生产部署与回滚手册

本文面向 Ubuntu 云服务器，目标架构为：公网 HTTPS → Nginx → 回环地址上的
`ar_server`，MySQL 保存用户、会话、浏览统计和作品互动，Redis保存匿名访客身份与
展馆在线状态。以下命令假定源码位于 `/opt/Jingjie-AR`，服务用户为 `jingjie`。

## 1. 发布前约束

- 只从已审核的 Git 提交构建，不在服务器源码目录手工改代码。
- krpano 运行文件来自已购买或获授权的私有包，不提交 Git，也不写入公开资源清单。
- `/etc/jingjie-ar.env` 权限设为 `root:jingjie 0640`；命令行、日志和截图不得出现数据库
  密码、用户密码、密码哈希、User Token 或 Visitor Token。执行期间不要启用 `set -x`。
- `sql/jingjie_ar_schema.sql` 仅包含 additive schema（`CREATE TABLE IF NOT EXISTS`）。
  新表上线后即使应用回滚也不删除；若未来迁移包含 `ALTER`，必须另行制定备份和回退方案。
- 全链路脚本会创建测试用户和互动数据，并要求在线人数从 0 开始，只能在隔离的预发布
  环境或正式开放流量前运行。

## 2. 安装依赖并准备目录

```bash
set -euo pipefail
sudo apt update
sudo apt install -y build-essential cmake pkg-config git git-lfs \
  libargon2-dev libmysqlclient-dev libhiredis-dev mysql-client nginx curl wrk
sudo git lfs install --system

sudo install -d -o jingjie -g jingjie -m 0755 \
  /opt/jingjie-ar /opt/jingjie-ar/releases /var/lib/jingjie-ar/logs
sudo install -d -o jingjie -g jingjie -m 0700 /var/backups/jingjie-ar
```

CMake 会同时查找 `argon2.h` 和 `libargon2`；开发文件缺失时配置阶段会明确失败，不会退回
SHA-256。Argon2id 固定使用 v1.3、64 MiB 内存、3 次迭代、并行度 1、16 字节随机盐和
32 字节哈希。

## 3. 备份当前版本

先确认当前软链接和工作树，再备份数据库、二进制、静态目录和配置。备份文件默认只允许
当前用户读取。

```bash
set -euo pipefail
sudo -u jingjie git -C /opt/Jingjie-AR status --short
readlink -f /opt/jingjie-ar/current || true

sudo -u jingjie bash -c '
  set -euo pipefail
  umask 077
  backup_id=$(date -u +%Y%m%dT%H%M%SZ)
  set -a; . /etc/jingjie-ar.env; set +a
  MYSQL_PWD="$MYSQL_PASSWORD" mysqldump \
    --single-transaction --routines --triggers \
    -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" \
    "$MYSQL_DATABASE" > "/var/backups/jingjie-ar/mysql-$backup_id.sql"
  if test -L /opt/jingjie-ar/current; then
    current=$(readlink -f /opt/jingjie-ar/current)
    test -d "$current"
    printf "%s\n" "$current" > "/var/backups/jingjie-ar/release-$backup_id.txt"
    cp --archive --dereference "$current/bin/ar_server" \
      "/var/backups/jingjie-ar/ar_server-$backup_id"
    tar -C "$current" -czf "/var/backups/jingjie-ar/www-$backup_id.tar.gz" www config
  fi
  cp --archive /etc/jingjie-ar.env "/var/backups/jingjie-ar/env-$backup_id"
'
```

若 `status --short` 有输出，先确认来源；不要用 `git reset --hard` 清除服务器上的未知修改。

## 4. 拉取、校验资源并构建版本目录

```bash
set -euo pipefail
sudo -u jingjie git -C /opt/Jingjie-AR pull --ff-only origin main
sudo -u jingjie git -C /opt/Jingjie-AR lfs pull
sudo -u jingjie git -C /opt/Jingjie-AR lfs fsck

cd /opt/Jingjie-AR
sudo -u jingjie ./scripts/provision_krpano.sh /opt/private/krpano/html/assets/krp
sudo -u jingjie bash tests/integration/assets_manifest_test.sh

sudo -u jingjie cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DHTTP_FRAMEWORK_WITH_MYSQL=ON -DHTTP_FRAMEWORK_WITH_REDIS=ON
sudo -u jingjie cmake --build build-release --target ar_server -j2

release_id=$(sudo -u jingjie git rev-parse --verify HEAD)
release_dir="/opt/jingjie-ar/releases/$release_id"
sudo -u jingjie test ! -e "$release_dir"
sudo -u jingjie install -d "$release_dir/bin" "$release_dir/config"
sudo -u jingjie install -m 0755 build-release/bin/ar_server "$release_dir/bin/ar_server"
sudo -u jingjie cp -a WebApps/ARServer/www "$release_dir/www"
sudo -u jingjie install -m 0644 WebApps/ARServer/config/exhibition.json \
  "$release_dir/config/exhibition.json"
sudo -u jingjie test -f "$release_dir/www/assets/krp/runtime/player_krp_v2.js"
```

二进制、前端和展馆配置放在同一个不可变版本目录中，切换 `/opt/jingjie-ar/current`
即可保持三者一致。不要把 `/opt/private/krpano` 或 `/etc/jingjie-ar.env` 加入仓库。

## 5. 执行 additive schema

仅在数据库备份成功后执行。该脚本保留现有数据，不删除旧场景表：

```bash
sudo -u jingjie bash -c '
  set -euo pipefail
  set -a; . /etc/jingjie-ar.env; set +a
  MYSQL_PWD="$MYSQL_PASSWORD" mysql \
    -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" "$MYSQL_DATABASE" \
    < /opt/Jingjie-AR/sql/jingjie_ar_schema.sql
'
```

在 `/etc/jingjie-ar.env` 中使用绝对路径，并让服务只监听回环地址：

```dotenv
AR_HOST=127.0.0.1
AR_PORT=8080
AR_STATIC_ROOT=/opt/jingjie-ar/current/www
AR_EXHIBITION_CONFIG=/opt/jingjie-ar/current/config/exhibition.json
AR_ALLOWED_ORIGIN=https://jingjiear.cn
```

## 6. systemd 常驻服务和原子切换

```ini
# /etc/systemd/system/jingjie-ar.service
[Unit]
Description=Jingjie AR krpano museum
After=network-online.target mysql.service redis-server.service
Wants=network-online.target

[Service]
Type=simple
User=jingjie
Group=jingjie
WorkingDirectory=/var/lib/jingjie-ar
EnvironmentFile=/etc/jingjie-ar.env
ExecStart=/opt/jingjie-ar/current/bin/ar_server
Restart=on-failure
RestartSec=3
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

首次发布或切换新版本：

```bash
set -euo pipefail
release_id=$(sudo -u jingjie git -C /opt/Jingjie-AR rev-parse --verify HEAD)
release_dir="/opt/jingjie-ar/releases/$release_id"
sudo -u jingjie test -x "$release_dir/bin/ar_server"
sudo -u jingjie test -f "$release_dir/www/index.html"
sudo -u jingjie ln -s "$release_dir" /opt/jingjie-ar/current.next
sudo -u jingjie mv -Tf /opt/jingjie-ar/current.next /opt/jingjie-ar/current

sudo systemctl daemon-reload
sudo systemctl enable jingjie-ar
sudo systemctl restart jingjie-ar
sudo systemctl status jingjie-ar --no-pager
curl --fail-with-body -sS http://127.0.0.1:8080/api/scenes >/dev/null
```

先在回环地址验证，确认 `ar_server` 正常后再重载 Nginx。

## 7. Nginx 缓存、安全头和精准限流

以下内容必须放在 `http {}` 中（例如
`/etc/nginx/conf.d/jingjie-ar-limits.conf`）。评论限流键只在 POST 时取客户端 IP，
因此读取评论的 GET 请求不会消耗写接口额度：

```nginx
limit_req_zone $binary_remote_addr zone=jingjie_auth:10m rate=5r/m;
limit_req_zone $binary_remote_addr zone=jingjie_visitor_init:10m rate=30r/m;

map $request_method $jingjie_comment_write_ip {
    default "";
    POST $binary_remote_addr;
}
limit_req_zone $jingjie_comment_write_ip zone=jingjie_comment:10m rate=10r/m;

# 访问日志只记录 $uri（不含 query string）；禁止改用会包含参数的
# $request 或 $request_uri，也不要记录 Authorization、Cookie 等请求头。
log_format jingjie_no_args
    '$remote_addr - $request_method $uri $server_protocol '
    '$status $body_bytes_sent $request_time';
```

创建 `/etc/nginx/snippets/jingjie-ar-proxy.conf`：

```nginx
proxy_pass http://127.0.0.1:8080;
proxy_http_version 1.1;
proxy_set_header Host $host;
proxy_set_header X-Real-IP $remote_addr;
proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
proxy_set_header X-Forwarded-Proto $scheme;
proxy_read_timeout 65s;
```

创建 `/etc/nginx/snippets/jingjie-ar-csp.conf`。这是 krpano 所需的最小同源策略，未开放
任何第三方域名：

```nginx
add_header Content-Security-Policy "default-src 'self'; img-src 'self' data: blob:; media-src 'self' blob:; script-src 'self' 'unsafe-eval'; style-src 'self' 'unsafe-inline'; connect-src 'self'; worker-src 'self' blob:" always;
```

站点配置 `/etc/nginx/sites-available/jingjie-ar`：

```nginx
server {
    listen 443 ssl http2;
    listen [::]:443 ssl http2;
    server_name jingjiear.cn;
    root /opt/jingjie-ar/current/www;
    client_max_body_size 2m;
    access_log /var/log/nginx/jingjie-ar.access.log jingjie_no_args;

    # ssl_certificate / ssl_certificate_key 由 Certbot 管理。
    include /etc/nginx/snippets/jingjie-ar-csp.conf;

    location = /api/auth {
        limit_req zone=jingjie_auth burst=5 nodelay;
        include /etc/nginx/snippets/jingjie-ar-proxy.conf;
        add_header Cache-Control "no-store" always;
        include /etc/nginx/snippets/jingjie-ar-csp.conf;
    }

    location = /api/visitors/session {
        limit_req zone=jingjie_visitor_init burst=10 nodelay;
        include /etc/nginx/snippets/jingjie-ar-proxy.conf;
        add_header Cache-Control "no-store" always;
        include /etc/nginx/snippets/jingjie-ar-csp.conf;
    }

    location ~ ^/api/artworks/[^/]+/comments$ {
        limit_req zone=jingjie_comment burst=10 nodelay;
        include /etc/nginx/snippets/jingjie-ar-proxy.conf;
        add_header Cache-Control "no-store" always;
        include /etc/nginx/snippets/jingjie-ar-csp.conf;
    }

    # 其他只读和写 API 不继承上述三个写接口的限流区。
    location /api/ {
        include /etc/nginx/snippets/jingjie-ar-proxy.conf;
        add_header Cache-Control "no-store" always;
        include /etc/nginx/snippets/jingjie-ar-csp.conf;
    }

    location ^~ /assets/ {
        alias /opt/jingjie-ar/current/www/assets/;
        expires 7d;
        add_header Cache-Control "public, max-age=604800, immutable" always;
        include /etc/nginx/snippets/jingjie-ar-csp.conf;
        access_log off;
    }

    location = /index.html {
        add_header Cache-Control "no-store, no-cache, must-revalidate" always;
        include /etc/nginx/snippets/jingjie-ar-csp.conf;
        try_files $uri =404;
    }

    location = / {
        add_header Cache-Control "no-store, no-cache, must-revalidate" always;
        include /etc/nginx/snippets/jingjie-ar-csp.conf;
        try_files /index.html =404;
    }

    location / {
        try_files $uri $uri/ /index.html;
    }
}
```

检查后平滑加载：

```bash
set -euo pipefail
sudo nginx -t
sudo systemctl reload nginx
curl -fsSI https://jingjiear.cn/index.html | grep -Ei 'cache-control|content-security-policy'
curl -fsSI https://jingjiear.cn/assets/pano/15949056/preview.jpg | grep -Ei 'cache-control|expires'
curl -fsSI https://jingjiear.cn/api/scenes | grep -Ei 'cache-control|content-security-policy'
```

## 8. 上线验收与压测

先验证 HTTPS API，再用浏览器检查场景切换、热点、作品弹窗、登录、点赞、评论、音乐与
手机方向控制。隔离环境可运行严格的八步全链路脚本：

```bash
set -euo pipefail
curl --fail-with-body -sS https://jingjiear.cn/api/scenes >/dev/null
BASE_URL=https://staging.jingjiear.cn \
  bash tests/integration/museum_end_to_end_test.sh
```

上线后从独立压测机执行 30 秒压测，保存原始输出并记录 RPS、P99、超时数和非 2xx 数；
不要用认证、访客初始化或评论写接口做无授权压力测试：

```bash
set -euo pipefail
wrk -t2 -c100 -d30s --latency https://jingjiear.cn/api/scenes \
  | tee "museum-read-$(date -u +%Y%m%dT%H%M%SZ).log"
```

同时观察 `journalctl -u jingjie-ar`、Nginx access/error log、CPU、内存、MySQL 与 Redis，
压测结果必须如实报告，不能把回环链路数据表述成公网数据。

## 9. 应用回滚

回滚只切回已验证的旧版本目录，不删除新表，也不恢复旧数据库覆盖上线后的用户数据：

```bash
set -euo pipefail
previous_release=$(sudo sed -n '1p' /var/backups/jingjie-ar/release-YYYYmmddTHHMMSSZ.txt)
case "$previous_release" in
  /opt/jingjie-ar/releases/*) ;;
  *) echo 'invalid previous release path' >&2; exit 1 ;;
esac
sudo -u jingjie test -x "$previous_release/bin/ar_server"
sudo -u jingjie test -f "$previous_release/www/index.html"
sudo -u jingjie ln -s "$previous_release" /opt/jingjie-ar/rollback.next
sudo -u jingjie mv -Tf /opt/jingjie-ar/rollback.next /opt/jingjie-ar/current
sudo systemctl restart jingjie-ar
curl --fail-with-body -sS http://127.0.0.1:8080/api/scenes >/dev/null
sudo nginx -t
sudo systemctl reload nginx
```

若新版本写入了向后不兼容的数据，先停止服务并按该次迁移单独审核的恢复方案处理；禁止
临时执行 `DROP TABLE`、删除数据库或 `git reset --hard`。
