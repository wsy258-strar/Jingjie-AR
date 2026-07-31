// 统一 API 成功响应：dataJson 必须是后端已经序列化完成的 JSON 值。
#pragma once

#include <http/HttpResponse.h>

#include <string>

namespace ar {

HttpResponse makeApiSuccess(const std::string& dataJson,
                            const std::string& message = std::string());

} // namespace ar
