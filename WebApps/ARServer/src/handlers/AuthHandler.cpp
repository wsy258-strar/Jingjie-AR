// 注册/登录 HTTP 处理实现：参数校验在边界完成，密码与会话逻辑交由 AuthService。
#include <handlers/AuthHandler.h>
#include <services/AuthService.h>
#include <utils/ApiError.h>
#include <utils/ApiResponse.h>

namespace {

bool jsonStringField(const std::string& body, const std::string& field, std::string* value)
{
    const std::string key = "\"" + field + "\"";
    const std::string::size_type found = body.find(key);
    if (found == std::string::npos) return false;
    std::string::size_type cursor = body.find(':', found + key.size());
    if (cursor == std::string::npos) return false;
    while (++cursor < body.size() && (body[cursor] == ' ' || body[cursor] == '\t' || body[cursor] == '\n' || body[cursor] == '\r')) {}
    if (cursor >= body.size() || body[cursor] != '"') return false;
    value->clear();
    for (++cursor; cursor < body.size(); ++cursor)
    {
        if (body[cursor] == '"') return true;
        if (body[cursor] != '\\') { value->push_back(body[cursor]); continue; }
        if (++cursor >= body.size()) return false;
        switch (body[cursor])
        {
        case '"': value->push_back('"'); break;
        case '\\': value->push_back('\\'); break;
        case '/': value->push_back('/'); break;
        case 'b': value->push_back('\b'); break;
        case 'f': value->push_back('\f'); break;
        case 'n': value->push_back('\n'); break;
        case 'r': value->push_back('\r'); break;
        case 't': value->push_back('\t'); break;
        default: return false;
        }
    }
    return false;
}

} // namespace

namespace ar {

bool AuthHandler::credentials(const HttpRequest& request, std::string* username, std::string* password)
{
    if (!username || !password) return false;
    if (!request.body().empty())
        return jsonStringField(request.body(), "username", username) &&
               jsonStringField(request.body(), "password", password) &&
               !username->empty() && !password->empty();
    const std::map<std::string, std::string>& parameters = request.queryParameters();
    const std::map<std::string, std::string>::const_iterator user = parameters.find("username");
    const std::map<std::string, std::string>::const_iterator pass = parameters.find("password");
    if (user == parameters.end() || pass == parameters.end() || user->second.empty() || pass->second.empty())
        return false;
    *username = user->second;
    *password = pass->second;
    return true;
}

bool AuthHandler::validate(const HttpRequest& request, HttpResponse* response)
{
    std::string username;
    std::string password;
    if (AuthHandler::credentials(request, &username, &password)) return true;
    if (response)
    {
        *response = makeApiError(HttpResponse::k400BadRequest, "INVALID_CREDENTIALS",
                                 "missing username or password",
                                 request.attribute("request_id"));
    }
    return false;
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
