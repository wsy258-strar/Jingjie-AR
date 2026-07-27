// HTTP 解析资源上限，避免恶意或异常请求无限占用连接缓冲和内存。
#pragma once

#include <cstddef>

/// 默认限制适用于普通 Web API；部署方可按业务负载通过 HttpServer 调整。
struct HttpLimits {
    size_t maxRequestLineBytes;
    size_t maxHeaderBytes;
    size_t maxBodyBytes;
    HttpLimits() : maxRequestLineBytes(8192), maxHeaderBytes(32768),
                   maxBodyBytes(1024 * 1024) {}
};
