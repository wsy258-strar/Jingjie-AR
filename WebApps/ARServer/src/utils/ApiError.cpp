#include <utils/ApiError.h>

#include <utils/JsonUtil.h>

namespace ar {

HttpResponse makeApiError(HttpResponse::HttpStatusCode status,
                          const std::string& code,
                          const std::string& message,
                          const std::string& requestId)
{
    HttpResponse response(false);
    response.setStatusCode(status);
    response.setContentType("application/json; charset=utf-8");
    std::string body = "{\"error\":\"" + JsonUtil::escape(message) +
                       "\",\"code\":\"" + JsonUtil::escape(code) + "\"";
    if (!requestId.empty())
        body += ",\"request_id\":\"" + JsonUtil::escape(requestId) + "\"";
    response.setBody(body + "}");
    return response;
}

} // namespace ar
