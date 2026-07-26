#pragma once

#include <middleware/Middleware.h>

namespace ar {

class AuthMiddleware : public Middleware
{
public:
    bool before(HttpRequest& request, HttpResponse& response) override;
    void after(const HttpRequest&, HttpResponse&) override {}
private:
    static bool isPublicRequest(const HttpRequest& request);
};

} // namespace ar
