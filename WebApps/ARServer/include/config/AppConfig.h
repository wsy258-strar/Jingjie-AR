// 应用配置模型：从环境变量读取监听、静态资源和数据后端参数，避免凭据进入源码。
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ar {

struct AppConfig
{
    AppConfig();
    std::string host;
    uint16_t port;
    int threads;
    int cacheCapacity;
    int dbWorkers;
    int cacheWorkers;
    size_t maxBodyBytes;
    bool logEnabled;
    size_t logRollSizeBytes;
    int logFlushIntervalSeconds;
    int logRetentionDays;
    std::string staticRoot;
    std::string exhibitionConfig;
    std::string allowedOrigin;
    std::string mysqlHost;
    uint16_t mysqlPort;
    std::string mysqlUser;
    std::string mysqlPassword;
    std::string mysqlDatabase;
    int mysqlPoolSize;
    std::string redisHost;
    uint16_t redisPort;
    int redisPoolSize;
    int sessionTtlSeconds;
    int testDbDelayMs;
    static bool fromEnvironment(bool mysqlEnabled, AppConfig* config, std::vector<std::string>* errors);
    static bool fromMap(const std::map<std::string, std::string>& environment, bool mysqlEnabled,
                        AppConfig* config, std::vector<std::string>* errors);
};

} // namespace ar
