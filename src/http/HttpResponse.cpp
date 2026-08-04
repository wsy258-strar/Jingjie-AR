// HTTP 响应序列化实现：保证头部、Content-Length 与正文/文件发送元数据保持一致。
#include <http/HttpResponse.h>
#include <stdio.h>
#include <cstring>

// ---------- 状态码 → 默认消息 ----------

const char* HttpResponse::statusMessage(int code)
{
    switch (code)
    {
    case 200: return "OK";
    case 204: return "No Content";
    case 206: return "Partial Content";
    case 301: return "Moved Permanently";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 416: return "Range Not Satisfiable";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    default:  return "Unknown";
    }
}

std::string HttpResponse::header(const std::string& name) const
{
    std::map<std::string, std::string>::const_iterator it = headers_.find(name);
    return it != headers_.end() ? it->second : std::string();
}

// ---------- 序列化 ----------

void HttpResponse::appendToBuffer(Buffer* output) const
{
    // 1. 状态行: "HTTP/1.1 200 OK\r\n"
    char buf[128];
    const char* msg = statusMessage_.empty() ? statusMessage(statusCode_) : statusMessage_.c_str();
    snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\n", statusCode_, msg);
    output->append(buf, strlen(buf));

    // 2. 判断是否需要自动补上 Content-Type / Content-Length / Connection
    bool hasContentType   = headers_.find("Content-Type")   != headers_.end();
    bool hasContentLength = headers_.find("Content-Length") != headers_.end();
    bool hasConnection    = headers_.find("Connection")     != headers_.end();

    // 3. Server 头
    {
        const char* s = "Server: webserver\r\n";
        output->append(s, strlen(s));
    }

    // 4. Content-Type (默认 text/html)
    if (!hasContentType)
    {
        const char* s = "Content-Type: text/html\r\n";
        output->append(s, strlen(s));
    }

    // 5. Content-Length (根据body长度计算)
    if (!hasContentLength)
    {
        snprintf(buf, sizeof(buf), "Content-Length: %zu\r\n", hasFile() ? fileCount_ : body_.size());
        output->append(buf, strlen(buf));
    }

    // 6. Connection
    if (!hasConnection)
    {
        if (closeConnection_)
        {
            const char* s = "Connection: close\r\n";
            output->append(s, strlen(s));
        }
        else
        {
            const char* s = "Connection: Keep-Alive\r\n";
            output->append(s, strlen(s));
        }
    }

    // 7. 用户自定义头部
    for (const auto& header : headers_)
    {
        output->append(header.first.c_str(),  header.first.size());
        output->append(": ", 2);
        output->append(header.second.c_str(), header.second.size());
        output->append("\r\n", 2);
    }

    // 8. 空行分隔
    output->append("\r\n", 2);

    // 9. Body
    if (!hasFile() && !body_.empty())
    {
        output->append(body_.c_str(), body_.size());
    }
}

// ---------- 错误响应工厂 ----------

HttpResponse HttpResponse::makeErrorResponse(HttpStatusCode code, bool close,
                                             const std::string& extraInfo)
{
    HttpResponse resp(close);
    resp.setStatusCode(code);

    const char* msg = statusMessage(code);
    std::string title = std::to_string(code) + " " + msg;

    std::string body;
    body += "<html><head><title>" + title + "</title></head>\r\n";
    body += "<body><center><h1>" + title + "</h1></center>\r\n";
    if (!extraInfo.empty())
    {
        body += "<center><p>" + extraInfo + "</p></center>\r\n";
    }
    body += "<hr><center>webserver</center>\r\n";
    body += "</body></html>\r\n";

    resp.setContentType("text/html");
    resp.setBody(body);
    return resp;
}
