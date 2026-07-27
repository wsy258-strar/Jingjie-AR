// API 错误工具：将业务/校验失败统一表示为稳定的 JSON 响应格式与 HTTP 状态码。
#pragma once

#include <http/HttpResponse.h>

#include <string>

namespace ar {

HttpResponse makeApiError(HttpResponse::HttpStatusCode status,
                          const std::string& code,
                          const std::string& message,
                          const std::string& requestId = std::string());

} // namespace ar
