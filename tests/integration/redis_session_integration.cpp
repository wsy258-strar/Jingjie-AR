#include <cache/RedisConnectionPool.h>
#include <session/RedisSessionStorage.h>

#include <cstdlib>
#include <string>

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        return 2;
    }
    const char* host = std::getenv("REDIS_HOST");
    const char* portText = std::getenv("REDIS_PORT");
    RedisConnectionPool pool(host ? host : "127.0.0.1", portText ? std::atoi(portText) : 6379, 1);
    http::session::RedisSessionStorage storage(&pool);
    const std::string id("integration-probe");
    const std::string action(argv[1]);

    if (action == "save")
    {
        http::session::Session session(id, 9876543210LL, 1800);
        session.setValue("binary", std::string("value\0bytes", 11));
        return storage.save(session) ? 0 : 1;
    }
    if (action == "load")
    {
        http::session::Session session;
        return storage.load(id, &session) && session.value("binary") == std::string("value\0bytes", 11)
            ? 0 : 1;
    }
    if (action == "remove")
    {
        if (!storage.remove(id))
        {
            return 1;
        }
        http::session::Session session;
        return storage.load(id, &session) ? 1 : 0;
    }
    return 2;
}
