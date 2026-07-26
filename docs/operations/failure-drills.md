# ARServer 故障演练

所有脚本默认只连接由 `BASE_URL`、`REDIS_PORT` 和 MySQL 环境变量明确指定的测试实例。禁止对生产 Redis/MySQL 运行。涉及 Redis 服务控制的脚本要求显式设置 `ALLOW_SERVICE_CONTROL=1`。

## Sanitizer 环境说明

ASan/UBSan 可使用 `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan` 运行。当前受限执行环境禁止 LeakSanitizer 的 ptrace 路径，并使 TSan 在启动时报告 `unexpected memory mapping`；该错误发生在测试主体执行前，使用受控沙箱外命令同样可复现。因此 TSan 二进制可以构建，但必须在允许 TSan 地址空间布局的 CI/Linux 主机上运行并记录无竞态结果，不能将这里的启动失败解释为项目竞态。

## Redis 回源

1. 使用测试帐户登录，保存返回的 `session_token`。
2. 删除测试 Redis 的 `session:{token}`。
3. `GET /api/session?token={token}` 必须仍成功；这证明 MySQL 回源可用。
4. 停止测试 Redis 后，Session 查询仍应可用；`/api/scenes/{sceneId}/members` 和心跳应返回 503。
5. 立即恢复 Redis，并检查测试实例的 `PING` 返回 `PONG`。

运行：

```bash
ALLOW_SERVICE_CONTROL=1 BASE_URL=http://127.0.0.1:8080 \
REDIS_PORT=6379 REDIS_STOP_COMMAND='redis-cli -p 6379 shutdown nosave' \
REDIS_START_COMMAND='redis-server --port 6379 --daemonize yes' \
tests/integration/redis_fallback_test.sh
```

## 已断开客户端

使用测试专用的 200 ms 数据库延迟路由发送请求后立即关闭客户端。等待一秒，确认 `ar_server` 仍存活，日志中不存在 `use-after-free`、`double-send` 或崩溃标记。此演练不对生产连接或数据执行写操作。

启动测试进程时显式设置 `AR_TEST_DB_DELAY_MS=200`，并提供一个活动测试 Session：

```bash
AR_SERVER_PID=$! AR_SERVER_LOG=/tmp/ar-server.log AR_TEST_PORT=8080 \
TEST_SESSION_TOKEN=... tests/integration/disconnected_client_test.sh
```

## 恢复

若任一演练异常：停止测试进程、启动测试 Redis、清除仅测试前缀的数据，并重新运行 `ctest`。不要删除共享 Redis 数据库或执行通配符 `DEL`。
