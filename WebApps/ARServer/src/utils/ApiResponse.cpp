// 标准 API 成功 JSON 构造实现，data 直接嵌入响应而不会二次编码。
#include <utils/ApiResponse.h>

#include <utils/JsonUtil.h>

namespace ar {

HttpResponse makeApiSuccess(const std::string& dataJson,
                            const std::string& message)
{
    HttpResponse response(false);
    response.setStatusCode(HttpResponse::k200Ok);
    response.setContentType("application/json; charset=utf-8");
    const std::string data = dataJson.empty() ? "null" : dataJson;
    response.setBody("{\"success\":true,\"data\":" + data +
                     ",\"message\":\"" + JsonUtil::escape(message) + "\"}");
    return response;
}

} // namespace ar
