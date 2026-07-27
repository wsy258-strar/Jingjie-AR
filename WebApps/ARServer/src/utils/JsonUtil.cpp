// JSON 字符串转义、字段提取与轻量序列化实现。
#include <utils/JsonUtil.h>

namespace ar {

std::string JsonUtil::escape(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it)
    {
        const unsigned char character = static_cast<unsigned char>(*it);
        switch (character)
        {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (character < 0x20)
            {
                const char digits[] = "0123456789abcdef";
                escaped += "\\u00";
                escaped += digits[(character >> 4) & 0x0f];
                escaped += digits[character & 0x0f];
            }
            else
            {
                escaped += static_cast<char>(character);
            }
        }
    }
    return escaped;
}

} // namespace ar
