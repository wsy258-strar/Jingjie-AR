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
