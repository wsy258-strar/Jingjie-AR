#pragma once

#include <cstddef>

struct HttpLimits {
    size_t maxRequestLineBytes;
    size_t maxHeaderBytes;
    size_t maxBodyBytes;
    HttpLimits() : maxRequestLineBytes(8192), maxHeaderBytes(32768),
                   maxBodyBytes(1024 * 1024) {}
};
