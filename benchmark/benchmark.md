# Legacy webserver 性能压测报告（框架化前基线）

## 测试环境

| 项目 | 值 |
|------|----|
| CPU | AMD Ryzen 9 7945HX (32) @ 2.50 GHz |
| 内存 | 7.6 GiB |
| OS | Linux 6.6 (WSL2) |
| 编译器 | g++ 11.4.0, C++11, Release -O2 |
| 日志 | 关闭 |
| 服务器线程数 | 3 |

## 测试方法

| 参数 | 长连接 | 短连接 |
|------|--------|--------|
| 工具 | wrk | ab (ApacheBench) |
| 协议 | HTTP/1.1 Keep-Alive | HTTP/1.0 (每请求新 TCP) |
| 目标 | `/js/app.js` (~959B) | `/js/app.js` (~959B) |
| 并发梯度 | 10 / 50 / 100 / 200 / 500 | 10 / 50 / 100 / 200 / 500 |
| 持续时间 | 15s / 梯度 | 5000 请求 / 梯度 |
| 预热 | curl × 10 | curl × 5 |
| LFU 缓存 | 命中 | 命中 |

> **长连接**：一个 TCP 连接复用多次请求，测 IO 吞吐上限。  
> **短连接**：每个请求建立新 TCP（握手 → 请求 → 响应 → 挥手），测建连 + IO 综合能力。

---

## 一、长连接（HTTP/1.1 Keep-Alive）

| 并发 | QPS | P50 | P75 | P90 | P99 | Avg | 错误 |
|------|-----|-----|-----|-----|-----|-----|------|
| 10 | 49,396 | 138µs | 200µs | 276µs | 492µs | 161µs | 0 |
| 50 | 73,302 | 500µs | 605µs | 820µs | 1.28ms | 551µs | 0 |
| 100 | **97,871** | 0.95ms | 1.11ms | 1.38ms | 2.11ms | 1.01ms | 0 |
| 200 | 74,468 | 2.12ms | 2.35ms | 2.65ms | 4.06ms | 2.19ms | 0 |
| 500 | 95,077 | 5.15ms | 5.54ms | 5.95ms | 7.24ms | 5.23ms | 0 |

```
峰值 QPS: 97,871  (c=100)
最佳 P50: 138µs   (c=10)
最佳 P99: 492µs   (c=10)
最差 P99: 7.24ms  (c=500)
```

## 二、短连接（HTTP/1.0）

| 并发 | QPS | Total | Connect | Process | 失败 |
|------|-----|-------|---------|----------|------|
| 10 | 4,094 | 2ms | 0ms | 2ms | 0 |
| 50 | **4,225** | 12ms | 0ms | 12ms | 0 |
| 100 | 4,209 | 23ms | 0ms | 23ms | 0 |
| 200 | 4,134 | 47ms | 0ms | 47ms | 0 |
| 500 | 4,065 | 116ms | 1ms | 115ms | 0 |

```
峰值 QPS: 4,225   (c=50)
总延迟范围: 2ms ~ 116ms
Connect:   ≈ 0ms  (localhost, 无网络延迟)
Process:   2ms ~ 115ms (随并发线性增长)
```

## 三、长/短连接对比

| 并发 | 长连接 QPS | 短连接 QPS | 比值 | 说明 |
|------|-----------|-----------|------|------|
| 10 | 49,396 | 4,094 | 8.3% | |
| 50 | 73,302 | 4,225 | 5.8% | |
| 100 | 97,871 | 4,209 | 4.3% | |
| 200 | 74,468 | 4,134 | 5.6% | |
| 500 | 95,077 | 4,065 | 4.3% | |

**短连接 QPS 约为长连接的 4~8%**，差值全部来自 TCP 三次握手 + 四次挥手开销，以及每个请求独立创建/销毁 `TcpConnection` 的成本。

---

## 四、延迟分析

### 长连接延迟分布 (c=100)

```
P50:  ████████████████▏ 0.95ms
P75:  ██████████████████▏ 1.11ms
P90:  ████████████████████▏ 1.38ms
P99:  ██████████████████████████████▏ 2.11ms
```

P50→P99 仅 2.2×，无尖刺延迟，事件循环调度均匀。

### 短连接延迟拆解 (c=100)

```
Total:    23ms
  ├── Connect:    0ms   (localhost loopback)
  └── Process:   23ms   (accept → parse → cache → send → shutdown)
```

Process 耗时包含完整链路：`accept()` → `HttpContext::parseRequest()` → `StaticFileCache::get()` → `Buffer::send()` → `TcpConnection::shutdown()`。

---

## 五、性能画像

```
IO 吞吐 (长连接):   ████████████████████████████████████  97,871 req/s
TCP建连+IO (短连接): ██                                    4,225 req/s
```

| 指标 | 值 |
|------|----|
| 长连接峰值 QPS | 97,871 |
| 短连接峰值 QPS | 4,225 |
| 长连接最低 P50 | 138µs |
| 长连接最低 P99 | 492µs |
| 短连接最低 Total | 2ms |
| 测试期间崩溃 | 0 |
| 测试期间失败 | 0 |

---

## 六、优化历程

本次压测过程中发现并修复的关键问题：

| 问题 | 现象 | 根因 | 修复 |
|------|------|------|------|
| P99 尖刺 500ms+ | 长连接 P99 异常偏高 | 异步日志缓冲区刷盘阻塞 IO 线程 | 极限压测时关闭日志 |
| 短连接服务器崩溃 | ~18000 次连接后 SIGSEGV | `std::map<TcpConnection*, HttpContext>` 多线程数据竞争 | 将 HttpContext 移入 TcpConnection 成员 |
| 短连接 QPS 解析为 0 | awk 字段偏移错误 | ab 输出 `Requests per second:` 含空格，`$2` 取到 `per` | 改用 `grep -oP` 精确提取数字 |

---
# Reproducible framework and AR measurements

Run a release build, start `ar_server` with a test MySQL/Redis pair, perform ten warm-up requests, then run:

```bash
BASE_URL=http://127.0.0.1:8080 DURATION_SECONDS=15 benchmark/framework_overhead.sh
BASE_URL=http://127.0.0.1:8080 SESSION_TOKEN=... benchmark/ar_api.sh
```

For every result, record QPS, P50/P90/P99, error count, CPU usage, process thread count, payload size, compiler flags, cache state, and whether access logging was enabled. Benchmark the static route, dynamic route, CORS/access-log route, cached Session read, and member polling at concurrency 10, 100, and 500. Do not compare results produced on different machines or cache states.

## Current framework and ARServer evidence (2026-07-22)

The measurements below use the frameworkized `ar_server` Release build and are
separate from the legacy baseline above.

| Item | Value |
|------|-------|
| Host | Linux 6.6.87.2-microsoft-standard-WSL2, 32 logical CPUs |
| Build | g++ 11.4.0, C++11, Release (`-O3 -DNDEBUG`) |
| Server | `AR_THREADS=3`; process snapshot: 12 threads, 11,776 KiB RSS, 0.0% CPU before load |
| Dependencies | Isolated Redis plus configured test MySQL; Session was authenticated, entered, heartbeated and warmed |
| Request setup | `wrk -t2`, 15 seconds per point, 10 warm-up requests, 2-second client timeout |
| Cache/logging | Static cache warm; access-log and CORS middleware enabled; process output redirected to `/dev/null` so disk throughput is not measured |
| Static payload | `/` returns the 5,460-byte `index.html` |

Framework routes:

| Path | Concurrency | QPS | P50 | P90 | P99 | Errors |
|------|------------:|----:|----:|----:|----:|-------:|
| direct callback `/api/scenes` | 10 | 27,421.02 | 334µs | 607µs | 930µs | 0 |
| direct callback `/api/scenes` | 100 | 42,116.14 | 2.31ms | 2.85ms | 4.62ms | 0 |
| direct callback `/api/scenes` | 500 | 44,987.08 | 10.99ms | 12.01ms | 13.09ms | 0 |
| static file `/` | 10 | 16,818.31 | 627µs | 940µs | 1.27ms | 10 timeouts |
| static file `/` | 100 | 34,062.19 | 2.72ms | 4.21ms | 5.95ms | 0 |
| static file `/` | 500 | 34,218.81 | 14.13ms | 16.87ms | 28.80ms | 2,541 non-2xx/3xx |
| dynamic route `/api/scenes/1` | 10 | 29,098.58 | 314µs | 567µs | 860µs | 0 |
| dynamic route `/api/scenes/1` | 100 | 36,503.14 | 2.66ms | 3.31ms | 5.36ms | 0 |
| dynamic route `/api/scenes/1` | 500 | 38,004.00 | 13.01ms | 14.52ms | 16.04ms | 0 |
| CORS + access log `/api/scenes/1` | 10 | 27,075.95 | 338µs | 613µs | 930µs | 0 |
| CORS + access log `/api/scenes/1` | 100 | 37,638.53 | 2.59ms | 3.21ms | 5.16ms | 0 |
| CORS + access log `/api/scenes/1` | 500 | 44,638.16 | 10.96ms | 12.47ms | 14.99ms | 0 |

AR API routes:

| Path | Concurrency | QPS | P50 | P90 | P99 | Errors |
|------|------------:|----:|----:|----:|----:|-------:|
| cached `GET /api/session` | 10 | 13,310.51 | 704µs | 940µs | 490.01ms | 0 |
| cached `GET /api/session` | 100 | 19,485.86 | 5.66ms | 6.27ms | 6.85ms | 100 timeouts |
| cached `GET /api/session` | 500 | 47,853.78 | 9.04ms | 16.07ms | 19.81ms | 0 |
| `GET /api/scenes/1/members` | 10 | 7,510.74 | 1.31ms | 1.52ms | 1.84ms | 0 |
| `GET /api/scenes/1/members` | 100 | 8,167.08 | 12.07ms | 13.13ms | 13.64ms | 0 |
| `GET /api/scenes/1/members` | 500 | 49,472.85 | 7.92ms | 17.87ms | 26.23ms | 553,646 non-2xx/3xx |

Errors are intentionally retained: they describe the bounded-worker and
connection-pressure boundary of this localhost run.  These values are not a
production network-capacity claim.  Re-run the scripts above on the target host
before deployment decisions.
