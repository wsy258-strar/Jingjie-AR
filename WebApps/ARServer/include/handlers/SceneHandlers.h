// 场景查询端点处理器：提供场景目录与单个场景详情的 HTTP 适配。
#pragma once

#include <http/HttpRequest.h>
#include <http/HttpResponse.h>

namespace ar {
class SceneHandlers {
public:
    static void list(const HttpRequest&, HttpResponse* response);
    static void get(const HttpRequest& request, HttpResponse* response);
    static void interactions(const HttpRequest&, HttpResponse* response);
};
}
