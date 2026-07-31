// ARServer 进程入口：加载环境配置、构造 EventLoop 与应用，并启动 HTTP 监听。
#include <ARServer.h>
#include <catalog/ExhibitionCatalog.h>
#include <config/AppConfig.h>
#include <handlers/ArtworkInteractionHandlers.h>
#include <handlers/AuthHandler.h>
#include <handlers/SceneHandlers.h>
#include <handlers/VisitorHandlers.h>
#include <services/ArtworkInteractionService.h>
#include <services/AuthService.h>
#include <services/DaoAuthStore.h>
#include <services/DaoSessionStore.h>
#include <services/PresenceService.h>
#include <services/SessionService.h>
#include <services/VisitorSessionService.h>

#include <base/TaskWorkerPool.h>
#include <http/StaticFileHandler.h>
#include <log/AsyncLogging.h>
#include <log/LogDirectory.h>
#include <log/Logger.h>
#include <net/EventLoop.h>
#include <net/InetAddress.h>

#ifdef HAS_MYSQL
#include <db/ArtworkInteractionDAO.h>
#include <db/DBWorkerPool.h>
#include <db/ExhibitionStatisticsDAO.h>
#include <db/MySQLConnectionPool.h>
#include <db/SessionDAO.h>
#endif
#ifdef HAS_REDIS
#include <cache/RedisConnectionPool.h>
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
    std::unique_ptr<TaskWorkerPool> fileWorkers(
        new TaskWorkerPool(config.cacheWorkers, 128));
    StaticFileHandler files(config.staticRoot, StaticFileHandler::CacheGet(),
                            StaticFileHandler::CachePut(), fileWorkers.get(), 1024 * 1024);
#ifdef HAS_MYSQL
    MySQLConnectionPool::ConnInfo mysqlInfo = {
        config.mysqlHost, config.mysqlPort, config.mysqlUser,
        config.mysqlPassword, config.mysqlDatabase};
    std::unique_ptr<MySQLConnectionPool> mysqlPool(
        new MySQLConnectionPool(mysqlInfo, config.mysqlPoolSize));
    std::unique_ptr<DBWorkerPool> dbWorkers(
        new DBWorkerPool(mysqlPool.get(), config.dbWorkers));
    std::unique_ptr<SessionDAO> sessionDao(new SessionDAO(dbWorkers.get()));
    std::unique_ptr<ArtworkInteractionDAO> artworkDao(
        new ArtworkInteractionDAO(dbWorkers.get()));
    std::unique_ptr<ExhibitionStatisticsDAO> statisticsDao(
        new ExhibitionStatisticsDAO(dbWorkers.get()));
    std::unique_ptr<ar::DaoAuthStore> authStore(new ar::DaoAuthStore(sessionDao.get()));
    std::unique_ptr<ar::DaoSessionStore> sessionStore(
        new ar::DaoSessionStore(sessionDao.get()));
#endif

#ifdef HAS_REDIS
    std::unique_ptr<RedisConnectionPool> redisPool(new RedisConnectionPool(
        config.redisHost, config.redisPort, config.redisPoolSize));
    std::unique_ptr<ar::RedisVisitorStore> visitorStore(
        new ar::RedisVisitorStore(redisPool.get()));
    std::unique_ptr<ar::RedisPresenceStore> presenceStore(
        new ar::RedisPresenceStore(redisPool.get()));
#endif

#ifdef HAS_MYSQL
    ar::AuthService authService(authStore.get());
    ar::SessionService sessionService(sessionStore.get());
#else
    ar::AuthService authService(0);
    ar::SessionService sessionService(0);
#endif
    ar::ArtworkInteractionService artworkService(
        catalog.get(), &sessionService,
#ifdef HAS_MYSQL
        artworkDao.get()
#else
        0
#endif
    );
    ar::VisitorSessionService visitorService(
#ifdef HAS_REDIS
        visitorStore.get()
#else
        0
#endif
    );
    ar::PresenceService presenceService(
#ifdef HAS_REDIS
        presenceStore.get()
#else
        0
#endif
    );

    std::unique_ptr<TaskWorkerPool> visitorWorkers(
        new TaskWorkerPool(config.cacheWorkers, 128));
    ar::AuthHandler authHandler(&authService);
    ar::SceneHandlers sceneHandlers(catalog.get());
    ar::ArtworkInteractionHandlers artworkHandlers(&artworkService);
    ar::VisitorHandlers visitorHandlers(
        &visitorService, &presenceService,
#ifdef HAS_MYSQL
        statisticsDao.get(),
#else
        0,
#endif
        visitorWorkers.get(), catalog->exhibitionId());

    {
        ar::ARServer server(&loop, InetAddress(config.port, config.host), config.allowedOrigin,
                            &authHandler, &visitorHandlers, &sceneHandlers,
                            &artworkHandlers, &files);
        server.setThreadNum(config.threads);
        server.start();
        loop.loop();
    }

    // 先停止接受请求，再依次排空文件、访客和数据库任务，避免异步回调越过依赖生命周期。
    fileWorkers.reset();
    visitorWorkers.reset();
#ifdef HAS_MYSQL
    dbWorkers.reset();
#endif
    if (logging)
    {
        Logger::setOutput([](const char* message, int length) {
            fwrite(message, 1, length, stdout);
        });
        logging->stop();
    }
    return 0;
}
