// 注册/登录 HTTP 处理实现：参数校验在边界完成，密码与会话逻辑交由 AuthService。
#include <handlers/AuthHandler.h>
#include <services/AuthService.h>
#include <services/SessionService.h>
#include <utils/ApiError.h>
#include <utils/ApiResponse.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace {

const size_t kMaxUsernameBytes = 64;
const size_t kMaxPasswordBytes = 128;

bool validCredentialLengths(const std::string& username, const std::string& password)
{
    return !username.empty() && username.size() <= kMaxUsernameBytes &&
           !password.empty() && password.size() <= kMaxPasswordBytes;
}

bool jsonContentType(const HttpRequest& request)
{
    std::string contentType = request.getHeader("Content-Type");
    std::transform(contentType.begin(), contentType.end(), contentType.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    const std::string::size_type semicolon = contentType.find(';');
    std::string mediaType = contentType.substr(0, semicolon);
    const std::string whitespace(" \t\r\n");
    const std::string::size_type mediaBegin = mediaType.find_first_not_of(whitespace);
    const std::string::size_type mediaEnd = mediaType.find_last_not_of(whitespace);
    if (mediaBegin == std::string::npos ||
        mediaType.substr(mediaBegin, mediaEnd - mediaBegin + 1) != "application/json")
        return false;
    for (std::string::size_type begin = semicolon;
         begin != std::string::npos && begin < contentType.size();)
    {
        ++begin;
        const std::string::size_type end = contentType.find(';', begin);
        std::string parameter = contentType.substr(begin, end - begin);
        const std::string::size_type first = parameter.find_first_not_of(whitespace);
        const std::string::size_type last = parameter.find_last_not_of(whitespace);
        if (first == std::string::npos) return false;
        parameter = parameter.substr(first, last - first + 1);
        const std::string::size_type equals = parameter.find('=');
        if (equals == std::string::npos || equals == 0 || equals + 1 == parameter.size())
            return false;
        begin = end;
    }
    return true;
}

} // namespace

namespace ar {

bool AuthHandler::credentials(const HttpRequest& request, std::string* username, std::string* password)
{
    if (!username || !password) return false;
    if (!request.body().empty())
    {
        const nlohmann::json value = nlohmann::json::parse(request.body(), 0, false);
        if (value.is_discarded() || !value.is_object() ||
            !value.contains("username") || !value["username"].is_string() ||
            !value.contains("password") || !value["password"].is_string())
            return false;
        *username = value["username"].get<std::string>();
        *password = value["password"].get<std::string>();
        return validCredentialLengths(*username, *password);
    }
    const std::map<std::string, std::string>& parameters = request.queryParameters();
    const std::map<std::string, std::string>::const_iterator user = parameters.find("username");
    const std::map<std::string, std::string>::const_iterator pass = parameters.find("password");
    if (user == parameters.end() || pass == parameters.end() || user->second.empty() || pass->second.empty())
        return false;
    *username = user->second;
    *password = pass->second;
    return validCredentialLengths(*username, *password);
}

bool AuthHandler::validate(const HttpRequest& request, HttpResponse* response)
{
    std::string username;
    std::string password;
    if ((!request.body().empty() && !jsonContentType(request)) ||
        !AuthHandler::credentials(request, &username, &password))
    {
        if (response)
        {
            *response = makeApiError(HttpResponse::k400BadRequest, "INVALID_CREDENTIALS",
                                     "missing username or password",
                                     request.attribute("request_id"));
        }
        return false;
    }
    return true;
}

void AuthHandler::logout(const HttpRequest& request, const AsyncResponder& responder) const
{
    if (!sessions_)
    {
        responder.send(makeApiError(HttpResponse::k503ServiceUnavailable,
                                    "SERVICE_UNAVAILABLE", "session service unavailable",
                                    request.attribute("request_id")));
        return;
    }
    const std::string token = request.attribute("auth.token");
    const std::string requestId = request.attribute("request_id");
    sessions_->logout(token, [responder, requestId](SessionService::LogoutResult result) {
        if (result == SessionService::kLogoutOk)
        {
            responder.send(makeApiSuccess("{}"));
            return;
        }
        responder.send(makeApiError(HttpResponse::k503ServiceUnavailable,
                                    "SERVICE_UNAVAILABLE", "session logout unavailable", requestId));
    });
}

void AuthHandler::handle(const HttpRequest& request, const AsyncResponder& responder) const
{
    HttpResponse invalid(false);
    if (!validate(request, &invalid)) { responder.send(invalid); return; }
    if (!service_)
    {
        responder.send(makeApiError(HttpResponse::k503ServiceUnavailable,
                                    "SERVICE_UNAVAILABLE", "authentication service unavailable",
                                    request.attribute("request_id")));
        return;
    }
    std::string username;
    std::string password;
    AuthHandler::credentials(request, &username, &password);
    const std::string requestId = request.attribute("request_id");
    service_->authenticate(username, password,
        [responder, requestId](const AuthResult& result, int status) {
            if (status == 200)
                responder.send(makeApiSuccess(AuthService::json(result)));
            else if (status == 401)
                responder.send(makeApiError(HttpResponse::k401Unauthorized, "INVALID_PASSWORD",
                                            "invalid password", requestId));
            else
                responder.send(makeApiError(HttpResponse::k503ServiceUnavailable,
                                            "SERVICE_UNAVAILABLE",
                                            "authentication service unavailable", requestId));
        });
}

} // namespace ar
