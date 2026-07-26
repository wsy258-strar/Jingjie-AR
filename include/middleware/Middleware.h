#pragma once

#include <http/HttpRequest.h>
#include <http/HttpResponse.h>

class Middleware
{
public:
    virtual ~Middleware() {}

    virtual bool before(HttpRequest& request, HttpResponse& response) = 0;
    virtual void after(const HttpRequest& request, HttpResponse& response) = 0;
};
