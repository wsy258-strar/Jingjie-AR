#include "TestSupport.h"

#include <log/Logger.h>

#include <string>

namespace {

std::string output;

void capture(const char* data, int len)
{
    output.append(data, static_cast<size_t>(len));
}

void testDebugOutputIsSuppressedAtInfoThreshold()
{
    Logger::setOutput(capture);
    Logger::setLogLevel(Logger::INFO);

    output.clear();
    LOG_DEBUG << "poll-loop-detail";
    CHECK(output.empty());

    LOG_INFO << "server-started";
    CHECK(output.find("server-started") != std::string::npos);
}

} // namespace

int main()
{
    testDebugOutputIsSuppressedAtInfoThreshold();
    return 0;
}
