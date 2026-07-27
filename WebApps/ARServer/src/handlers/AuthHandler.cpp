// 注册/登录 HTTP 处理实现：参数校验在边界完成，密码与会话逻辑交由 AuthService。
#include <handlers/AuthHandler.h>
#include <services/AuthService.h>

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
        response->setStatusCode(HttpResponse::k400BadRequest);
        response->setContentType("application/json; charset=utf-8");
        response->setBody("{\"error\":\"missing username or password\"}");
    }
    return false;
}

void AuthHandler::handle(const HttpRequest& request, const AsyncResponder& responder) const
{
    HttpResponse invalid(false);
    if (!validate(request, &invalid)) { responder.send(invalid); return; }
    if (!service_) { HttpResponse response(false); response.setStatusCode(HttpResponse::k503ServiceUnavailable); responder.send(response); return; }
    std::string username;
    std::string password;
    AuthHandler::credentials(request, &username, &password);
    service_->authenticate(username, password,
        [responder](const AuthResult& result, int status) {
            HttpResponse response(false);
            response.setStatusCode(static_cast<HttpResponse::HttpStatusCode>(status));
            response.setContentType("application/json; charset=utf-8");
            if (status == 200) response.setBody(AuthService::json(result));
            else if (status == 401) response.setBody("{\"error\":\"invalid password\"}");
            else response.setBody("{\"error\":\"service unavailable\"}");
            responder.send(response);
        });
}

} // namespace ar
