#pragma once

#include <string>
#include <vector>

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
