#include "TestSupport.h"

#include <utils/ApiResponse.h>
#include <utils/ApiError.h>

int main()
{
    HttpResponse ok = ar::makeApiSuccess("{\"value\":7}");
    CHECK(ok.statusCode() == HttpResponse::k200Ok);
    CHECK(ok.header("Content-Type") == "application/json; charset=utf-8");
    CHECK(ok.body() ==
          "{\"success\":true,\"data\":{\"value\":7},\"message\":\"\"}");

    HttpResponse message = ar::makeApiSuccess("[1,2]", "已加载");
    CHECK(message.body() ==
          "{\"success\":true,\"data\":[1,2],\"message\":\"已加载\"}");

    HttpResponse error = ar::makeApiError(HttpResponse::k404NotFound,
                                          "SCENE_NOT_FOUND",
                                          "scene \"missing\" not found",
                                          "request-1");
    CHECK(error.statusCode() == HttpResponse::k404NotFound);
    CHECK(error.header("Content-Type") == "application/json; charset=utf-8");
    CHECK(error.body() ==
          "{\"success\":false,\"data\":null,\"message\":\"scene \\\"missing\\\" not found\","
          "\"code\":\"SCENE_NOT_FOUND\",\"requestId\":\"request-1\"}");

    HttpResponse errorWithoutRequestId =
        ar::makeApiError(HttpResponse::k400BadRequest,
                         "INVALID_REQUEST",
                         "line one\nline two");
    CHECK(errorWithoutRequestId.body() ==
          "{\"success\":false,\"data\":null,\"message\":\"line one\\nline two\","
          "\"code\":\"INVALID_REQUEST\",\"requestId\":\"\"}");
    return 0;
}
