// JSON 辅助工具：集中负责字符串转义与请求/响应字段的轻量编解码。
#pragma once

#include <string>

namespace ar {

class JsonUtil
{
public:
    static std::string escape(const std::string& value);
};

} // namespace ar
