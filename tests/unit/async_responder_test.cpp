#include "TestSupport.h"

#include <http/AsyncResponder.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace {

void testResponderSendsOnlyOnceAcrossThreads()
{
    std::atomic<int> sendCount(0);
    std::string sentBody;
    AsyncResponder responder([&sendCount, &sentBody](HttpResponse response) {
        ++sendCount;
        sentBody = response.body();
    });

    HttpResponse first(false);
    first.setBody("first");
    HttpResponse second(false);
    second.setBody("second");

    CHECK(responder.send(first));
    std::vector<std::thread> threads;
    for (int index = 0; index < 8; ++index)
    {
        threads.push_back(std::thread([&responder, &second]() {
            responder.send(second);
        }));
    }
    for (std::vector<std::thread>::iterator it = threads.begin(); it != threads.end(); ++it)
    {
        it->join();
    }

    CHECK(sendCount.load() == 1);
    CHECK(sentBody == "first");
    CHECK(!responder.send(second));
    CHECK(sendCount.load() == 1);
}

void testInvalidResponderDoesNotSend()
{
    AsyncResponder responder;
    HttpResponse response(false);
    CHECK(!responder.valid());
    CHECK(!responder.send(response));
}

} // namespace

int main()
{
    testResponderSendsOnlyOnceAcrossThreads();
    testInvalidResponderDoesNotSend();
    return 0;
}
