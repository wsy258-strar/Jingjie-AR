#pragma once

#include <string>

namespace ar {

class JsonUtil
{
public:
    static std::string escape(const std::string& value);
};

} // namespace ar
