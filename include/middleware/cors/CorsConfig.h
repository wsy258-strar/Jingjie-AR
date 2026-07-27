// CORS 策略配置：显式列出可接受的源、方法和请求头，避免默认放开跨域访问。
#pragma once

#include <string>
#include <vector>

/// allowCredentials 为 true 时调用方应避免配置通配符源。
struct CorsConfig
{
    CorsConfig()
        : allowCredentials(false),
          maxAge(0)
    {}

    std::vector<std::string> allowedOrigins;
    std::vector<std::string> allowedMethods;
    std::vector<std::string> allowedHeaders;
    bool allowCredentials;
    int maxAge;
};
