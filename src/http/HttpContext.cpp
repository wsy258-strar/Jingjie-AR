// HTTP 增量解析状态机实现，严格限制请求行、头部和消息体的资源消耗。
#include <http/HttpContext.h>
#include <Logger.h>
#include <string.h>
#include <algorithm>
#include <limits>

namespace {

enum ContentLengthParseResult
{
    kContentLengthOk,
    kContentLengthInvalid,
    kContentLengthOverflow
};

ContentLengthParseResult parseContentLength(const std::string& text, size_t* value)
{
    if (text.empty())
    {
        return kContentLengthInvalid;
    }

    size_t result = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c < '0' || c > '9')
        {
            return kContentLengthInvalid;
        }
        const size_t digit = static_cast<size_t>(c - '0');
        if (result > (std::numeric_limits<size_t>::max() - digit) / 10)
        {
            return kContentLengthOverflow;
        }
        result = result * 10 + digit;
    }
    *value = result;
    return kContentLengthOk;
}

bool exceedsLimit(size_t current, size_t additional, size_t limit)
{
    return current > limit || additional > limit - current;
}

} // namespace

// ========== 工具函数 ==========

const char* HttpContext::findCRLF(const char* begin, const char* end)
{
    for (const char* p = begin; p + 1 < end; ++p)
    {
        if (*p == '\r' && *(p + 1) == '\n')
        {
            return p; // 返回'\r'的位置
        }
    }
    return nullptr;
}

HttpRequest::Method HttpContext::stringToMethod(const std::string& m)
{
    if (m == "GET")     return HttpRequest::kGet;
    if (m == "POST")    return HttpRequest::kPost;
    if (m == "HEAD")    return HttpRequest::kHead;
    if (m == "PUT")     return HttpRequest::kPut;
    if (m == "DELETE")  return HttpRequest::kDelete;
    if (m == "OPTIONS") return HttpRequest::kOptions;
    return HttpRequest::kInvalid;
}

// ========== 解析入口 ==========

bool HttpContext::parseRequest(Buffer* buf, Timestamp receiveTime)
{
    // 仅在首次进入时设置接收时间
    if (state_ == kExpectRequestLine)
    {
        request_.setReceiveTime(receiveTime);
    }

    while (state_ != kGotCompleteRequest)
    {
        switch (state_)
        {
        case kExpectRequestLine:
        {
            const char* begin = buf->peek();
            const char* end   = begin + buf->readableBytes();
            const char* crlf  = findCRLF(begin, end);

            if (!crlf)
            {
                if (buf->readableBytes() > limits_.maxRequestLineBytes)
                {
                    error_ = kRequestLineTooLarge;
                    return false;
                }
                // 还没有收到完整的请求行，等待更多数据
                return true;
            }

            if (static_cast<size_t>(crlf - begin + 2) > limits_.maxRequestLineBytes)
            {
                error_ = kRequestLineTooLarge;
                return false;
            }

            if (!processRequestLine(begin, crlf))
            {
                error_ = kBadRequest;
                return false; // 请求行格式错误 → 400
            }

            // 消费请求行(包含\r\n)
            buf->retrieve(crlf - begin + 2);
            state_ = kExpectHeaders;
            break;
        }

        case kExpectHeaders:
        {
            if (!parseHeaders(buf))
            {
                if (error_ == kParseOk)
                {
                    error_ = kBadRequest;
                }
                return false; // 头部格式错误 → 400
            }
            // parseHeaders() 内部会更新 state_
            break;
        }

        case kExpectBody:
        {
            if (!parseBody(buf))
            {
                if (error_ == kParseOk)
                {
                    error_ = kBadRequest;
                }
                return false;
            }
            // parseBody() 内部会更新 state_
            break;
        }

        case kGotCompleteRequest:
            break; // 不应该到达这里
        }
    }

    return true;
}

// ========== 请求行解析 ==========

bool HttpContext::processRequestLine(const char* begin, const char* end)
{
    // 提取三个令牌: METHOD SP PATH?QUERY SP VERSION
    // 例如: "GET /index.html?key=val HTTP/1.1"

    const char* start = begin;
    const char* space1 = nullptr;
    const char* space2 = nullptr;

    for (const char* p = begin; p < end; ++p)
    {
        if (*p == ' ')
        {
            if (!space1)
            {
                space1 = p;
            }
            else if (!space2)
            {
                space2 = p;
                break; // 只需要前两个空格
            }
        }
    }

    if (!space1 || !space2)
    {
        return false; // 格式错误: "METHOD URL VERSION" 至少需要两个空格
    }

    // 1. 方法
    std::string methodStr(start, space1 - start);
    HttpRequest::Method method = stringToMethod(methodStr);
    if (method == HttpRequest::kInvalid)
    {
        LOG_WARN << "Unknown HTTP method: " << methodStr;
        return false;
    }
    request_.setMethod(method);

    // 2. URL (path + optional query)
    std::string url(space1 + 1, space2 - space1 - 1);
    size_t questionPos = url.find('?');
    if (questionPos != std::string::npos)
    {
        request_.setPath(url.substr(0, questionPos));
        request_.setQuery(url.substr(questionPos + 1));
    }
    else
    {
        request_.setPath(url);
        // query保持空字符串
    }

    // 3. HTTP版本
    request_.setVersion(std::string(space2 + 1, end - space2 - 1));

    return true;
}

// ========== 头部解析 ==========

bool HttpContext::parseHeaders(Buffer* buf)
{
    while (true)
    {
        const char* begin = buf->peek();
        const char* end   = begin + buf->readableBytes();
        const char* crlf  = findCRLF(begin, end);

        if (!crlf)
        {
            if (exceedsLimit(headerBytes_, buf->readableBytes(), limits_.maxHeaderBytes))
            {
                error_ = kHeadersTooLarge;
                return false;
            }
            // 头部还没收完，等待更多数据
            // state_ 保持 kExpectHeaders
            return true;
        }

        const size_t lineBytes = static_cast<size_t>(crlf - begin + 2);
        if (exceedsLimit(headerBytes_, lineBytes, limits_.maxHeaderBytes))
        {
            error_ = kHeadersTooLarge;
            return false;
        }

        // 检查是否为 空行(仅\r\n)，表示头部结束
        if (crlf == begin)
        {
            headerBytes_ += lineBytes;
            // 消费空行
            buf->retrieve(2);

            if (!request_.getHeader("transfer-encoding").empty())
            {
                error_ = kUnsupportedTransferEncoding;
                return false;
            }

            // 判断是否需要读body
            const std::map<std::string, std::string>& headers = request_.headers();
            const std::map<std::string, std::string>::const_iterator length =
                headers.find("content-length");
            if (length != headers.end())
            {
                const ContentLengthParseResult result = parseContentLength(length->second,
                                                                            &contentLength_);
                if (result == kContentLengthInvalid)
                {
                    error_ = kBadRequest;
                    return false;
                }
                if (result == kContentLengthOverflow)
                {
                    error_ = kBodyTooLarge;
                    return false;
                }
                if (contentLength_ > limits_.maxBodyBytes)
                {
                    error_ = kBodyTooLarge;
                    return false;
                }
                if (contentLength_ > 0)
                {
                    state_ = kExpectBody;
                }
                else
                {
                    state_ = kGotCompleteRequest;
                }
            }
            else
            {
                // GET / HEAD / DELETE 等通常没有 body
                state_ = kGotCompleteRequest;
            }
            return true;
        }

        // 解析一行header: "Key: Value\r\n"
        const char* colon = nullptr;
        for (const char* p = begin; p < crlf; ++p)
        {
            if (*p == ':')
            {
                colon = p;
                break;
            }
        }

        if (!colon)
        {
            // 非法header行(没有冒号)
            LOG_WARN << "Invalid header line (no colon)";
            return false;
        }

        std::string key(begin, colon - begin);
        // 跳过冒号后面的空格
        const char* valueStart = colon + 1;
        while (valueStart < crlf && *valueStart == ' ')
        {
            ++valueStart;
        }
        std::string value(valueStart, crlf - valueStart);

        std::string lowerKey = key;
        std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);
        if (lowerKey == "transfer-encoding" && !value.empty())
        {
            error_ = kUnsupportedTransferEncoding;
            return false;
        }

        request_.addHeader(key, value);

        // 消费这一行
        headerBytes_ += lineBytes;
        buf->retrieve(lineBytes);
    }
}

// ========== Body解析 ==========

bool HttpContext::parseBody(Buffer* buf)
{
    if (buf->readableBytes() < contentLength_)
    {
        // body还没收全, 等待更多数据
        return true;
    }

    // 读取body
    request_.setBody(std::string(buf->peek(), contentLength_));
    buf->retrieve(contentLength_);
    state_ = kGotCompleteRequest;
    return true;
}

// ========== 重置 ==========

void HttpContext::reset()
{
    HttpRequest dummy;
    request_.swap(dummy);
    state_          = kExpectRequestLine;
    contentLength_  = 0;
    headerBytes_    = 0;
    error_          = kParseOk;
}
