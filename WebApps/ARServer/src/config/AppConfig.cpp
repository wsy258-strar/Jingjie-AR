// 环境变量配置解析实现：启动期尽早验证必填项，避免运行时带着无效连接参数继续服务。
#include <config/AppConfig.h>

#include <cstdlib>
#include <cerrno>
#include <climits>

namespace {

bool parseUnsigned(const std::string& text, unsigned long long maximum,
                   unsigned long long* value)
{
    if (text.empty() || !value) return false;
    for (size_t index = 0; index < text.size(); ++index)
        if (text[index] < '0' || text[index] > '9') return false;
    errno = 0;
    char* end = 0;
    unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end != '\0' || parsed > maximum) return false;
    *value = parsed;
    return true;
}

void readString(const std::map<std::string, std::string>& environment, const char* name,
                std::string* value)
{
    std::map<std::string, std::string>::const_iterator found = environment.find(name);
    if (found != environment.end() && !found->second.empty()) *value = found->second;
}

void readLogEnabled(const std::map<std::string, std::string>& environment, bool* value)
{
    std::map<std::string, std::string>::const_iterator found = environment.find("AR_LOG_ENABLED");
    if (found != environment.end() && found->second == "false") *value = false;
}

bool readNumber(const std::map<std::string, std::string>& environment, const char* name,
                unsigned long long minimum, unsigned long long maximum,
                unsigned long long* value, std::vector<std::string>* errors)
{
    std::map<std::string, std::string>::const_iterator found = environment.find(name);
    if (found == environment.end() || found->second.empty()) return true;
    unsigned long long parsed = 0;
    if (!parseUnsigned(found->second, maximum, &parsed) || parsed < minimum)
    {
        errors->push_back(std::string(name) + " must be an integer between " +
                          std::to_string(minimum) + " and " + std::to_string(maximum));
        return false;
    }
    *value = parsed;
    return true;
}

} // namespace

namespace ar {

AppConfig::AppConfig()
    : host("0.0.0.0"), port(8080), threads(3), cacheCapacity(200), dbWorkers(4),
      cacheWorkers(4), maxBodyBytes(1048576), logEnabled(true),
      logRollSizeBytes(100U * 1024U * 1024U), logFlushIntervalSeconds(3),
      logRetentionDays(7), staticRoot("WebApps/ARServer/www"),
      exhibitionConfig("WebApps/ARServer/config/exhibition.json"),
      mysqlHost("127.0.0.1"), mysqlPort(3306), mysqlUser("ar_app"),
      mysqlDatabase("webserver"), mysqlPoolSize(8), redisHost("127.0.0.1"),
      redisPort(6379), redisPoolSize(8), sessionTtlSeconds(1800), testDbDelayMs(0) {}

bool AppConfig::fromMap(const std::map<std::string, std::string>& environment, bool mysqlEnabled,
                        AppConfig* config, std::vector<std::string>* errors)
{
    if (!config || !errors) return false;
    *config = AppConfig(); errors->clear();
    readString(environment, "AR_HOST", &config->host);
    readString(environment, "AR_STATIC_ROOT", &config->staticRoot);
    readString(environment, "AR_EXHIBITION_CONFIG", &config->exhibitionConfig);
    readString(environment, "AR_ALLOWED_ORIGIN", &config->allowedOrigin);
    readString(environment, "MYSQL_HOST", &config->mysqlHost);
    readString(environment, "MYSQL_USER", &config->mysqlUser);
    readString(environment, "MYSQL_DATABASE", &config->mysqlDatabase);
    readString(environment, "REDIS_HOST", &config->redisHost);
    readLogEnabled(environment, &config->logEnabled);
    std::map<std::string, std::string>::const_iterator password = environment.find("MYSQL_PASSWORD");
    if (password != environment.end()) config->mysqlPassword = password->second;
    unsigned long long value = config->port;
    if (readNumber(environment, "AR_PORT", 1, 65535, &value, errors)) config->port = static_cast<uint16_t>(value);
    value = config->threads;
    if (readNumber(environment, "AR_THREADS", 1, INT_MAX, &value, errors)) config->threads = static_cast<int>(value);
    value = config->cacheCapacity;
    if (readNumber(environment, "AR_CACHE_CAPACITY", 1, INT_MAX, &value, errors)) config->cacheCapacity = static_cast<int>(value);
    value = config->mysqlPort;
    if (readNumber(environment, "MYSQL_PORT", 1, 65535, &value, errors)) config->mysqlPort = static_cast<uint16_t>(value);
    value = config->mysqlPoolSize;
    if (readNumber(environment, "MYSQL_POOL_SIZE", 1, INT_MAX, &value, errors)) config->mysqlPoolSize = static_cast<int>(value);
    value = config->redisPort;
    if (readNumber(environment, "REDIS_PORT", 1, 65535, &value, errors)) config->redisPort = static_cast<uint16_t>(value);
    value = config->redisPoolSize;
    if (readNumber(environment, "REDIS_POOL_SIZE", 1, INT_MAX, &value, errors)) config->redisPoolSize = static_cast<int>(value);
    value = config->dbWorkers;
    if (readNumber(environment, "DB_WORKERS", 1, INT_MAX, &value, errors)) config->dbWorkers = static_cast<int>(value);
    value = config->cacheWorkers;
    if (readNumber(environment, "CACHE_WORKERS", 1, INT_MAX, &value, errors)) config->cacheWorkers = static_cast<int>(value);
    value = config->sessionTtlSeconds;
    if (readNumber(environment, "SESSION_TTL_SECONDS", 1, INT_MAX, &value, errors)) config->sessionTtlSeconds = static_cast<int>(value);
    value = config->maxBodyBytes;
    if (readNumber(environment, "MAX_BODY_BYTES", 1, static_cast<unsigned long long>(SIZE_MAX), &value, errors)) config->maxBodyBytes = static_cast<size_t>(value);
    value = config->testDbDelayMs;
    if (readNumber(environment, "AR_TEST_DB_DELAY_MS", 0, 1000, &value, errors)) config->testDbDelayMs = static_cast<int>(value);
    value = config->logRollSizeBytes / (1024U * 1024U);
    if (readNumber(environment, "AR_LOG_ROLL_SIZE_MB", 1, 1024, &value, errors))
        config->logRollSizeBytes = static_cast<size_t>(value) * 1024U * 1024U;
    value = config->logFlushIntervalSeconds;
    if (readNumber(environment, "AR_LOG_FLUSH_INTERVAL", 1, 3600, &value, errors))
        config->logFlushIntervalSeconds = static_cast<int>(value);
    value = config->logRetentionDays;
    if (readNumber(environment, "AR_LOG_RETENTION_DAYS", 0, 3650, &value, errors))
        config->logRetentionDays = static_cast<int>(value);
    if (config->allowedOrigin.find('*') != std::string::npos)
        errors->push_back("AR_ALLOWED_ORIGIN must be one exact origin without wildcards");
    if (mysqlEnabled && config->mysqlPassword.empty()) errors->push_back("MYSQL_PASSWORD is required");
    return errors->empty();
}

bool AppConfig::fromEnvironment(bool mysqlEnabled, AppConfig* config, std::vector<std::string>* errors)
{
    const char* names[] = {"AR_HOST", "AR_PORT", "AR_THREADS", "AR_STATIC_ROOT",
                           "AR_EXHIBITION_CONFIG", "AR_ALLOWED_ORIGIN", "AR_CACHE_CAPACITY",
                           "MYSQL_HOST", "MYSQL_PORT", "MYSQL_USER", "MYSQL_PASSWORD", "MYSQL_DATABASE",
                           "MYSQL_POOL_SIZE", "REDIS_HOST", "REDIS_PORT", "REDIS_POOL_SIZE", "DB_WORKERS",
                           "CACHE_WORKERS", "SESSION_TTL_SECONDS", "MAX_BODY_BYTES", "AR_TEST_DB_DELAY_MS",
                           "AR_LOG_ENABLED", "AR_LOG_ROLL_SIZE_MB", "AR_LOG_FLUSH_INTERVAL",
                           "AR_LOG_RETENTION_DAYS"};
    std::map<std::string, std::string> environment;
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
    {
        const char* value = std::getenv(names[index]);
        if (value) environment[names[index]] = value;
    }
    return fromMap(environment, mysqlEnabled, config, errors);
}

} // namespace ar
