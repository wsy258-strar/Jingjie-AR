#include "TestSupport.h"

#include <session/RedisSessionStorage.h>

#include <string>

namespace {

void testCodecRoundTripsBinarySessionValues()
{
    http::session::Session session("id|with\nnewline", 9876543210LL, 1800);
    const std::string binaryValue("pipe|newline\nnull\0utf8:\xE4\xB8\xAD", 26);
    session.setValue("binary", binaryValue);
    session.setValue(std::string("key|\n\0", 6), std::string("\0value", 6));

    const std::string encoded = http::session::RedisSessionStorage::encode(session);
    http::session::Session decoded;
    CHECK(http::session::RedisSessionStorage::decode(encoded, &decoded));
    CHECK(decoded.id() == session.id());
    CHECK(decoded.expiresAt() == session.expiresAt());
    CHECK(decoded.ttlSeconds() == session.ttlSeconds());
    CHECK(decoded.value("binary") == binaryValue);
    CHECK(decoded.value(std::string("key|\n\0", 6)) == std::string("\0value", 6));
}

void testCodecRejectsTrailingBytes()
{
    http::session::Session session("id", 1, 1);
    const std::string encoded = http::session::RedisSessionStorage::encode(session);
    http::session::Session decoded;
    CHECK(!http::session::RedisSessionStorage::decode(encoded + "x", &decoded));
}

} // namespace

int main()
{
    testCodecRoundTripsBinarySessionValues();
    testCodecRejectsTrailingBytes();
    return 0;
}
