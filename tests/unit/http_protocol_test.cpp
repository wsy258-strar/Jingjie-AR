#include "TestSupport.h"

#include <http/HttpContext.h>
#include <http/HttpLimits.h>
#include <http/HttpResponse.h>
#include <net/Buffer.h>

#include <string>

namespace {

void append(Buffer* buffer, const std::string& text)
{
    buffer->append(text.data(), text.size());
}

void testRequestModelAndParser()
{
    Buffer buffer;
    append(&buffer, "OPTIONS /widgets/7?format=json HTTP/1.1\r\n"
                    "Cookie: theme=dark; session=abc123\r\n"
                    "\r\n");

    HttpContext context;
    context.request().setPathParameter("widgetId", "7");
    context.request().setAttribute("traceId", "trace-1");
    CHECK(context.parseRequest(&buffer, Timestamp::now()));
    CHECK(context.gotAll());
    CHECK(context.error() == HttpContext::kParseOk);
    CHECK(context.request().method() == HttpRequest::kOptions);
    CHECK(context.request().pathParameter("widgetId") == "7");
    CHECK(context.request().cookie("session") == "abc123");
    CHECK(context.request().attribute("traceId") == "trace-1");
}

void testResponseGetters()
{
    HttpResponse response(false);
    response.setStatusCode(HttpResponse::k204NoContent);
    response.setBody("body");
    response.addHeader("X-Request-Id", "request-1");
    CHECK(response.statusCode() == HttpResponse::k204NoContent);
    CHECK(response.body() == "body");
    CHECK(response.header("X-Request-Id") == "request-1");
}

void testRequestLineLimit()
{
    Buffer buffer;
    append(&buffer, std::string(8193, 'G'));

    HttpContext context;
    CHECK(!context.parseRequest(&buffer, Timestamp::now()));
    CHECK(context.error() == HttpContext::kRequestLineTooLarge);
}

void testHeaderLimit()
{
    Buffer buffer;
    append(&buffer, "GET / HTTP/1.1\r\nX-Large: " + std::string(32769, 'a'));

    HttpContext context;
    CHECK(!context.parseRequest(&buffer, Timestamp::now()));
    CHECK(context.error() == HttpContext::kHeadersTooLarge);
}

void testBodyLimit()
{
    Buffer buffer;
    append(&buffer, "POST /upload HTTP/1.1\r\nContent-Length: 1048577\r\n\r\n");

    HttpContext context;
    CHECK(!context.parseRequest(&buffer, Timestamp::now()));
    CHECK(context.error() == HttpContext::kBodyTooLarge);
}

void testMalformedContentLength()
{
    Buffer buffer;
    append(&buffer, "POST /upload HTTP/1.1\r\nContent-Length:\r\n\r\n");

    HttpContext context;
    CHECK(!context.parseRequest(&buffer, Timestamp::now()));
    CHECK(context.error() == HttpContext::kBadRequest);
}

void testTransferEncoding()
{
    Buffer buffer;
    append(&buffer, "POST /upload HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");

    HttpContext context;
    CHECK(!context.parseRequest(&buffer, Timestamp::now()));
    CHECK(context.error() == HttpContext::kUnsupportedTransferEncoding);
}

void testDuplicateTransferEncodingCannotBypassRejection()
{
    Buffer buffer;
    append(&buffer, "POST /upload HTTP/1.1\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "Transfer-Encoding:\r\n"
                    "\r\n");

    HttpContext context;
    CHECK(!context.parseRequest(&buffer, Timestamp::now()));
    CHECK(context.error() == HttpContext::kUnsupportedTransferEncoding);
}

void testResetClearsParseError()
{
    Buffer invalid;
    append(&invalid, std::string(8193, 'G'));

    HttpContext context;
    CHECK(!context.parseRequest(&invalid, Timestamp::now()));
    CHECK(context.error() == HttpContext::kRequestLineTooLarge);

    context.reset();
    Buffer valid;
    append(&valid, "GET / HTTP/1.1\r\n\r\n");
    CHECK(context.parseRequest(&valid, Timestamp::now()));
    CHECK(context.gotAll());
    CHECK(context.error() == HttpContext::kParseOk);
}

} // namespace

int main()
{
    testRequestModelAndParser();
    testResponseGetters();
    testRequestLineLimit();
    testHeaderLimit();
    testBodyLimit();
    testMalformedContentLength();
    testTransferEncoding();
    testDuplicateTransferEncodingCannotBypassRejection();
    testResetClearsParseError();
    return 0;
}
