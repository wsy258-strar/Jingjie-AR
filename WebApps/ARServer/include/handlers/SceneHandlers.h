// 场景查询端点处理器：提供场景目录与单个场景详情的 HTTP 适配。
#pragma once

#include <http/HttpRequest.h>
#include <http/HttpResponse.h>

namespace ar {
class ExhibitionCatalog;

class SceneHandlers {
public:
    explicit SceneHandlers(const ExhibitionCatalog* catalog);
    void list(const HttpRequest& request, HttpResponse* response) const;
    void get(const HttpRequest& request, HttpResponse* response) const;
    static void interactions(const HttpRequest&, HttpResponse* response);

private:
    const ExhibitionCatalog* catalog_;
};
}
