#pragma once

#include <middleware/Middleware.h>

#include <string>

class AccessLogMiddleware : public Middleware
{
public:
    bool before(HttpRequest& request, HttpResponse& response) override;
    void after(const HttpRequest& request, HttpResponse& response) override;
    static std::string sanitizeTarget(const std::string& target);
};
