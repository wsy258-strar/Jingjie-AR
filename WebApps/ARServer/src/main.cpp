// ARServer 进程入口：加载环境配置、构造 EventLoop 与应用，并启动 HTTP 监听。
#include <ARServer.h>
#include <catalog/ExhibitionCatalog.h>
#include <handlers/AuthHandler.h>
#include <handlers/SceneHandlers.h>
#include <services/AuthService.h>
#include <services/DaoAuthStore.h>
#include <services/DaoSessionStore.h>
#include <services/CachedSessionStore.h>
#include <services/SessionService.h>
#include <handlers/SessionHandlers.h>
#include <handlers/PresenceHandlers.h>
#include <handlers/SceneInteractionHandlers.h>
#include <config/AppConfig.h>
#include <services/PresenceService.h>
#include <services/SceneInteractionService.h>

#include <base/TaskWorkerPool.h>
#include <http/StaticFileHandler.h>
#include <log/AsyncLogging.h>
#include <log/LogDirectory.h>
#include <log/Logger.h>
#include <net/EventLoop.h>
#include <net/InetAddress.h>

#ifdef HAS_MYSQL
#include <db/DBWorkerPool.h>
#include <db/MySQLConnectionPool.h>
#include <db/SessionDAO.h>
#endif
#ifdef HAS_REDIS
#include <cache/RedisConnectionPool.h>
#include <cache/SessionCache.h>
#endif

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>

int main()
{
    ar::AppConfig config;
    std::vector<std::string> configurationErrors;
    if (!ar::AppConfig::fromEnvironment(
#ifdef HAS_MYSQL
        true,
#else
        false,
#endif
        &config, &configurationErrors))
    {
        for (size_t index = 0; index < configurationErrors.size(); ++index)
            std::cerr << "configuration error: " << configurationErrors[index] << std::endl;
        return 2;
    }
    std::vector<std::string> catalogErrors;
    std::unique_ptr<ar::ExhibitionCatalog> catalog =
        ar::ExhibitionCatalog::load(config.exhibitionConfig, config.staticRoot, &catalogErrors);
    if (!catalog)
    {
        for (size_t index = 0; index < catalogErrors.size(); ++index)
            std::cerr << "exhibition configuration error: " << catalogErrors[index] << std::endl;
        return 2;
    }
    std::unique_ptr<AsyncLogging> logging;
    if (config.logEnabled)
    {
        std::string loggingError;
        if (!ensureLogDirectory("logs", &loggingError) ||
            !removeExpiredLogFiles("logs", config.logRetentionDays, &loggingError))
        {
            std::cerr << "logging initialization error: " << loggingError << std::endl;
            return 2;
        }
        logging.reset(new AsyncLogging("logs/ar_server", config.logRollSizeBytes,
                                       config.logFlushIntervalSeconds));
        Logger::setOutput(std::bind(&AsyncLogging::append, logging.get(),
                                    std::placeholders::_1, std::placeholders::_2));
        logging->start();
    }
    EventLoop loop;
    TaskWorkerPool fileWorkers(config.cacheWorkers, 128);
    StaticFileHandler files(config.staticRoot, StaticFileHandler::CacheGet(),
                            StaticFileHandler::CachePut(), &fileWorkers, 1024 * 1024);
#ifdef HAS_REDIS
    std::unique_ptr<RedisConnectionPool> redisPool(new RedisConnectionPool(
        config.redisHost, config.redisPort, config.redisPoolSize));
    std::unique_ptr<SessionCache> sessionCache(new SessionCache(redisPool.get()));
    std::unique_ptr<ar::SessionCacheAdapter> sessionCacheAdapter(
        new ar::SessionCacheAdapter(sessionCache.get()));
#endif
#ifdef HAS_MYSQL
    std::unique_ptr<MySQLConnectionPool> mysqlPool;
    std::unique_ptr<DBWorkerPool> dbWorkers;
    std::unique_ptr<SessionDAO> sessionDao;
    std::unique_ptr<ar::DaoAuthStore> authStore;
    std::unique_ptr<ar::DaoSessionStore> sessionStore;
    std::unique_ptr<ar::CachedSessionStore> cachedSessionStore;
    std::unique_ptr<SceneInteractionDAO> interactionDao;
    const char* password = config.mysqlPassword.c_str();
    if (password && *password)
    {
        MySQLConnectionPool::ConnInfo info = {config.mysqlHost.c_str(), config.mysqlPort,
                                               config.mysqlUser.c_str(), password,
                                               config.mysqlDatabase.c_str()};
        mysqlPool.reset(new MySQLConnectionPool(info, config.mysqlPoolSize));
        dbWorkers.reset(new DBWorkerPool(mysqlPool.get(), config.dbWorkers));
        sessionDao.reset(new SessionDAO(dbWorkers.get()));
        interactionDao.reset(new SceneInteractionDAO(dbWorkers.get()));
        authStore.reset(new ar::DaoAuthStore(sessionDao.get()));
        sessionStore.reset(new ar::DaoSessionStore(sessionDao.get()));
#ifdef HAS_REDIS
        cachedSessionStore.reset(new ar::CachedSessionStore(sessionStore.get(),
                                                             sessionCacheAdapter.get(), &fileWorkers));
#endif
    }
    ar::AuthService authService(authStore.get());
    ar::SessionService sessionService(
#ifdef HAS_REDIS
        cachedSessionStore.get()
#else
        sessionStore.get()
#endif
    );
#else
    ar::AuthService authService(0);
    ar::SessionService sessionService(0);
#endif
    ar::SceneInteractionService interactionService(&sessionService,
#ifdef HAS_MYSQL
                                                    interactionDao.get()
#else
                                                    0
#endif
    );
    ar::AuthHandler authHandler(&authService);
    std::unique_ptr<ar::RedisPresenceStore> presenceStore;
#ifdef HAS_REDIS
    presenceStore.reset(new ar::RedisPresenceStore(redisPool.get()));
#endif
    ar::PresenceService presenceService(presenceStore.get());
    ar::SessionHandlers sessionHandlers(&sessionService, &presenceService, &fileWorkers,
                                        config.testDbDelayMs);
    ar::PresenceHandlers presenceHandlers(&presenceService, &sessionService, &fileWorkers);
    ar::SceneHandlers sceneHandlers(catalog.get());
    ar::SceneInteractionHandlers interactionHandlers(&interactionService);
    ar::ARServer server(&loop, InetAddress(config.port), &authHandler, &sessionHandlers,
                        &presenceHandlers, &sceneHandlers, &interactionHandlers, &files);
    server.setThreadNum(config.threads);
    server.start();
    loop.loop();
    if (logging)
    {
        Logger::setOutput([](const char* message, int length) {
            fwrite(message, 1, length, stdout);
        });
        logging->stop();
    }
    return 0;
}
