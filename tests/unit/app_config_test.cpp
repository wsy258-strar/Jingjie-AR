#include "TestSupport.h"

#include <config/AppConfig.h>

#include <map>

int main()
{
    std::map<std::string, std::string> environment;
    ar::AppConfig config;
    std::vector<std::string> errors;
    CHECK(ar::AppConfig::fromMap(environment, false, &config, &errors));
    CHECK(config.host == "0.0.0.0");
    CHECK(config.port == 8080);
    CHECK(config.threads == 3);
    CHECK(config.cacheCapacity == 200);
    CHECK(config.dbWorkers == 4);
    CHECK(config.cacheWorkers == 4);
    CHECK(config.maxBodyBytes == 1048576);
    CHECK(config.staticRoot == "WebApps/ARServer/www");
    CHECK(config.testDbDelayMs == 0);
    CHECK(!ar::AppConfig::fromMap(environment, true, &config, &errors));
    CHECK(!errors.empty());

    environment["AR_PORT"] = "9090";
    environment["AR_THREADS"] = "6";
    environment["AR_CACHE_CAPACITY"] = "512";
    environment["MYSQL_HOST"] = "db.internal";
    environment["MYSQL_PORT"] = "3307";
    environment["MYSQL_USER"] = "ar_user";
    environment["MYSQL_PASSWORD"] = "safe-test-password";
    environment["MYSQL_DATABASE"] = "ar_data";
    environment["MYSQL_POOL_SIZE"] = "7";
    environment["REDIS_HOST"] = "cache.internal";
    environment["REDIS_PORT"] = "6380";
    environment["REDIS_POOL_SIZE"] = "8";
    environment["DB_WORKERS"] = "5";
    environment["CACHE_WORKERS"] = "9";
    environment["SESSION_TTL_SECONDS"] = "600";
    environment["MAX_BODY_BYTES"] = "2048";
    environment["AR_TEST_DB_DELAY_MS"] = "200";
    CHECK(ar::AppConfig::fromMap(environment, true, &config, &errors));
    CHECK(config.port == 9090);
    CHECK(config.threads == 6);
    CHECK(config.cacheCapacity == 512);
    CHECK(config.mysqlHost == "db.internal");
    CHECK(config.mysqlPort == 3307);
    CHECK(config.mysqlUser == "ar_user");
    CHECK(config.mysqlDatabase == "ar_data");
    CHECK(config.mysqlPoolSize == 7);
    CHECK(config.redisHost == "cache.internal");
    CHECK(config.redisPort == 6380);
    CHECK(config.redisPoolSize == 8);
    CHECK(config.dbWorkers == 5);
    CHECK(config.cacheWorkers == 9);
    CHECK(config.sessionTtlSeconds == 600);
    CHECK(config.maxBodyBytes == 2048);
    CHECK(config.testDbDelayMs == 200);

    environment["AR_PORT"] = "0";
    CHECK(!ar::AppConfig::fromMap(environment, false, &config, &errors));
    CHECK(!errors.empty());
    return 0;
}
