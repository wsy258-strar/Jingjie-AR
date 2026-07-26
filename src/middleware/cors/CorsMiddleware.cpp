#include <middleware/cors/CorsMiddleware.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {

std::string toLower(const std::string& value)
{
    std::string lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

bool contains(const std::vector<std::string>& values, const std::string& value,
              bool caseInsensitive)
{
    const std::string wanted = caseInsensitive ? toLower(value) : value;
    for (std::vector<std::string>::const_iterator it = values.begin(); it != values.end(); ++it)
    {
        const std::string candidate = caseInsensitive ? toLower(*it) : *it;
        if (candidate == wanted)
            return true;
    }
    return false;
}

std::string join(const std::vector<std::string>& values)
{
    std::ostringstream output;
    for (std::vector<std::string>::size_type i = 0; i < values.size(); ++i)
    {
        if (i != 0)
            output << ", ";
        output << values[i];
    }
    return output.str();
}

std::vector<std::string> splitCommaSeparated(const std::string& value)
{
    std::vector<std::string> tokens;
    std::istringstream input(value);
    std::string token;
    while (std::getline(input, token, ','))
    {
        const std::string::size_type first = token.find_first_not_of(" \t");
        if (first == std::string::npos)
            continue;
        const std::string::size_type last = token.find_last_not_of(" \t");
        tokens.push_back(token.substr(first, last - first + 1));
    }
    return tokens;
}

void addVaryOrigin(HttpResponse& response)
{
    const std::string vary = response.header("Vary");
    if (vary.empty())
        response.addHeader("Vary", "Origin");
    else if (!contains(splitCommaSeparated(vary), "Origin", true))
        response.addHeader("Vary", vary + ", Origin");
}

} // namespace

CorsMiddleware::CorsMiddleware(const CorsConfig& config)
    : config_(config)
{}

bool CorsMiddleware::before(HttpRequest& request, HttpResponse& response)
{
    if (request.method() != HttpRequest::kOptions)
        return true;

    const std::string origin = request.getHeader("Origin");
    const std::string requestedMethod = request.getHeader("Access-Control-Request-Method");
    if (origin.empty() && requestedMethod.empty())
        return true;

    if (origin.empty() || requestedMethod.empty() || !isOriginAllowed(origin) ||
        !isMethodAllowed(requestedMethod) ||
        !areHeadersAllowed(request.getHeader("Access-Control-Request-Headers")))
    {
        response.setStatusCode(HttpResponse::k403Forbidden);
        return false;
    }

    response.setStatusCode(HttpResponse::k204NoContent);
    addCorsHeaders(origin, response, true);
    return false;
}

void CorsMiddleware::after(const HttpRequest& request, HttpResponse& response)
{
    const std::string origin = request.getHeader("Origin");
    if (!origin.empty() && isOriginAllowed(origin))
        addCorsHeaders(origin, response, false);
}

bool CorsMiddleware::isOriginAllowed(const std::string& origin) const
{
    if (origin.empty())
        return false;
    if (contains(config_.allowedOrigins, origin, false))
        return true;
    return !config_.allowCredentials && contains(config_.allowedOrigins, "*", false);
}

bool CorsMiddleware::isMethodAllowed(const std::string& method) const
{
    return contains(config_.allowedMethods, method, false);
}

bool CorsMiddleware::areHeadersAllowed(const std::string& headers) const
{
    const std::vector<std::string> requested = splitCommaSeparated(headers);
    if (requested.empty())
        return true;
    if (contains(config_.allowedHeaders, "*", true))
        return true;
    for (std::vector<std::string>::const_iterator it = requested.begin(); it != requested.end(); ++it)
    {
        if (!contains(config_.allowedHeaders, *it, true))
            return false;
    }
    return true;
}

void CorsMiddleware::addCorsHeaders(const std::string& origin, HttpResponse& response,
                                    bool preflight) const
{
    const bool wildcard = !config_.allowCredentials &&
                          contains(config_.allowedOrigins, "*", false);
    response.addHeader("Access-Control-Allow-Origin", wildcard ? "*" : origin);
    if (!wildcard)
        addVaryOrigin(response);
    if (config_.allowCredentials)
        response.addHeader("Access-Control-Allow-Credentials", "true");
    if (preflight)
    {
        response.addHeader("Access-Control-Allow-Methods", join(config_.allowedMethods));
        if (!config_.allowedHeaders.empty())
            response.addHeader("Access-Control-Allow-Headers", join(config_.allowedHeaders));
        response.addHeader("Access-Control-Max-Age", std::to_string(config_.maxAge));
    }
}
