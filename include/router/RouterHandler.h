#pragma once

#include <http/HttpRequest.h>
#include <http/HttpResponse.h>

namespace http {
namespace router {

class RouterHandler
{
public:
    virtual ~RouterHandler() {}

    virtual void handle(const HttpRequest& request, HttpResponse* response) = 0;
};

} // namespace router
} // namespace http
