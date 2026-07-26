#pragma once

#include <middleware/Middleware.h>
#include <middleware/cors/CorsConfig.h>

class CorsMiddleware : public Middleware
{
public:
    explicit CorsMiddleware(const CorsConfig& config);

    bool before(HttpRequest& request, HttpResponse& response) override;
    void after(const HttpRequest& request, HttpResponse& response) override;

private:
    bool isOriginAllowed(const std::string& origin) const;
    bool isMethodAllowed(const std::string& method) const;
    bool areHeadersAllowed(const std::string& headers) const;
    void addCorsHeaders(const std::string& origin, HttpResponse& response,
                        bool preflight) const;

    CorsConfig config_;
};
