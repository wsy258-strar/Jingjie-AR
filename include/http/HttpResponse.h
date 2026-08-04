// HTTP/1.x 响应构建器：路由填充语义数据，协议层统一序列化并决定连接是否关闭。
#pragma once

#include <string>
#include <map>
#include <sys/types.h>

#include <net/Buffer.h>
#include <base/noncopyable.h>

/**
 * @brief HTTP响应构建器
 *
 * 用户路由回调通过此对象设置状态码、Content-Type、Body等，
 * HttpServer负责调用appendToBuffer()将其序列化后发送。
 */
class HttpResponse
{
public:
    enum HttpStatusCode
    {
        k200Ok                  = 200,
        k204NoContent           = 204,
        k206PartialContent      = 206,
        k301MovedPermanently    = 301,
        k304NotModified          = 304,
        k400BadRequest          = 400,
        k401Unauthorized        = 401,
        k403Forbidden           = 403,
        k404NotFound            = 404,
        k405MethodNotAllowed    = 405,
        k409Conflict            = 409,
        k413PayloadTooLarge     = 413,
        k416RangeNotSatisfiable = 416,
        k500InternalServerError = 500,
        k501NotImplemented      = 501,
        k503ServiceUnavailable  = 503,
    };

    explicit HttpResponse(bool close)
        : statusCode_(k200Ok),
          closeConnection_(close)
    {}

    // ---------- setters (用户路由回调使用) ----------
    void setStatusCode(HttpStatusCode code) { statusCode_ = code; }
    void setStatusMessage(const std::string& message) { statusMessage_ = message; }
    void setCloseConnection(bool on) { closeConnection_ = on; }
    bool closeConnection() const { return closeConnection_; }

    void setContentType(const std::string& contentType)
    { addHeader("Content-Type", contentType); }

    void setBody(const std::string& body) { body_ = body; }
    /// 标记文件响应；TcpConnection 后续通过 sendfile 分段发送该区间。
    void setFile(const std::string& path, off_t offset, size_t count)
    { filePath_ = path; fileOffset_ = offset; fileCount_ = count; }
    bool hasFile() const { return !filePath_.empty(); }
    const std::string& filePath() const { return filePath_; }
    off_t fileOffset() const { return fileOffset_; }
    size_t fileCount() const { return fileCount_; }

    void addHeader(const std::string& key, const std::string& value)
    { headers_[key] = value; }

    HttpStatusCode statusCode() const { return statusCode_; }
    const std::string& body() const { return body_; }
    std::string header(const std::string& name) const;

    // ---------- 序列化 ----------
    /// 将完整的HTTP响应(状态行+头部+空行+body)追加到output
    void appendToBuffer(Buffer* output) const;

    // ---------- 静态工厂 ----------
    /// 构建一个包含简单HTML错误页面的Response
    static HttpResponse makeErrorResponse(HttpStatusCode code, bool close,
                                          const std::string& extraInfo = "");

private:
    /// 根据状态码返回默认的状态消息字符串
    static const char* statusMessage(int code);

    HttpStatusCode statusCode_;
    std::string statusMessage_;
    std::map<std::string, std::string> headers_;
    std::string body_;
    std::string filePath_;
    off_t fileOffset_ = 0;
    size_t fileCount_ = 0;
    bool closeConnection_;
};
