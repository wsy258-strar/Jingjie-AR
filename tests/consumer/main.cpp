#include <http/HttpServer.h>
#include <net/EventLoop.h>
#include <net/InetAddress.h>

#include <cstdlib>

int main()
{
    const char* configuredPort = std::getenv("CONSUMER_PORT");
    const int port = configuredPort ? std::atoi(configuredPort) : 18081;
    if (port < 1 || port > 65535) return 2;
    EventLoop loop;
    HttpServer server(&loop, InetAddress(static_cast<uint16_t>(port)), "consumer");
    server.Get("/health", [](const HttpRequest&, HttpResponse* response) {
        response->setBody("ok");
    });
    server.start();
    loop.loop();
    return 0;
}
