#pragma once

#include <http/HttpResponse.h>

#include <string>

namespace ar {

HttpResponse makeApiError(HttpResponse::HttpStatusCode status,
                          const std::string& code,
                          const std::string& message,
                          const std::string& requestId = std::string());

} // namespace ar
