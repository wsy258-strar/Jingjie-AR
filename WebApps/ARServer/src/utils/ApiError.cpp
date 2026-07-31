// 标准 API 错误 JSON 构造实现。
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
    response.setBody("{\"success\":false,\"data\":null,\"message\":\"" +
                     JsonUtil::escape(message) + "\",\"code\":\"" +
                     JsonUtil::escape(code) + "\",\"requestId\":\"" +
                     JsonUtil::escape(requestId) + "\"}");
    return response;
}

} // namespace ar
