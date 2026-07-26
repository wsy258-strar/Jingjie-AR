#include "TestSupport.h"

#include <http/HttpContext.h>
#include <http/HttpServer.h>

#include <string>

namespace {

void append(Buffer* buffer, const std::string& text)
{
    buffer->append(text.data(), text.size());
}

void testClosingResponseStopsPipelinedDispatch()
{
    Buffer buffer;
    append(&buffer, "GET /close HTTP/1.1\r\n\r\n"
                    "GET /must-not-run HTTP/1.1\r\n\r\n");

    HttpContext context;
    int dispatchCount = 0;
    while (buffer.readableBytes() > 0)
    {
        CHECK(context.parseRequest(&buffer, Timestamp::invalid()));
        CHECK(context.gotAll());

        ++dispatchCount;
        const bool closeConnection = (dispatchCount == 1);
        if (!HttpServer::shouldContinueParsing(HttpDispatcher::kComplete,
                                               closeConnection))
        {
            break;
        }
        context.reset();
    }

    CHECK(dispatchCount == 1);
    CHECK(buffer.readableBytes() > 0);
}

void testKeepAliveResponseContinuesPipelinedDispatch()
{
    CHECK(HttpServer::shouldContinueParsing(HttpDispatcher::kComplete, false));
}

void testAsyncResponseStopsPipelinedDispatch()
{
    CHECK(!HttpServer::shouldContinueParsing(HttpDispatcher::kAsyncPending, false));
}

} // namespace

int main()
{
    testClosingResponseStopsPipelinedDispatch();
    testKeepAliveResponseContinuesPipelinedDispatch();
    testAsyncResponseStopsPipelinedDispatch();
    return 0;
}
