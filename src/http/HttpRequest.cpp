// HTTP 请求辅助实现：路径参数、Cookie、属性和查询字符串的懒解析均限定在单次请求。
#include <http/HttpRequest.h>
#include <sstream>
#include <iomanip>

namespace {

std::string trim(const std::string& value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
    {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
    {
        --end;
    }
    return value.substr(begin, end - begin);
}

} // namespace

/// 简单的十六进制字符转数值，失败返回 -1
static int hexDigitValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

std::string urlDecode(const std::string& str)
{
    std::string result;
    result.reserve(str.size());

    for (size_t i = 0; i < str.size(); ++i)
    {
        if (str[i] == '%' && i + 2 < str.size())
        {
            int high = hexDigitValue(str[i + 1]);
            int low  = hexDigitValue(str[i + 2]);
            if (high >= 0 && low >= 0)
            {
                result.push_back(static_cast<char>(high * 16 + low));
                i += 2;
                continue;
            }
        }
        if (str[i] == '+')
        {
            result.push_back(' ');
            continue;
        }
        result.push_back(str[i]);
    }
    return result;
}

const std::map<std::string, std::string>& HttpRequest::queryParameters() const
{
    if (queryParsed_)
    {
        return queryParams_;
    }

    queryParsed_ = true;
    const std::string& q = query_;
    if (q.empty()) return queryParams_;

    std::istringstream stream(q);
    std::string pair;
    while (std::getline(stream, pair, '&'))
    {
        if (pair.empty()) continue;

        size_t eqPos = pair.find('=');
        if (eqPos != std::string::npos)
        {
            std::string key   = urlDecode(pair.substr(0, eqPos));
            std::string value = urlDecode(pair.substr(eqPos + 1));
            queryParams_[key] = value;
        }
        else
        {
            queryParams_[urlDecode(pair)] = "";
        }
    }
    return queryParams_;
}

void HttpRequest::setPathParameter(const std::string& name, const std::string& value)
{
    pathParameters_[name] = value;
}

std::string HttpRequest::pathParameter(const std::string& name) const
{
    std::unordered_map<std::string, std::string>::const_iterator it = pathParameters_.find(name);
    return it != pathParameters_.end() ? it->second : std::string();
}

std::string HttpRequest::cookie(const std::string& name) const
{
    const std::string cookies = getHeader("cookie");
    size_t begin = 0;
    while (begin < cookies.size())
    {
        size_t end = cookies.find(';', begin);
        if (end == std::string::npos)
        {
            end = cookies.size();
        }

        const std::string pair = trim(cookies.substr(begin, end - begin));
        const size_t equals = pair.find('=');
        if (equals != std::string::npos && trim(pair.substr(0, equals)) == name)
        {
            return trim(pair.substr(equals + 1));
        }
        begin = end + 1;
    }
    return std::string();
}

void HttpRequest::setAttribute(const std::string& name, const std::string& value)
{
    attributes_[name] = value;
}

std::string HttpRequest::attribute(const std::string& name) const
{
    std::unordered_map<std::string, std::string>::const_iterator it = attributes_.find(name);
    return it != attributes_.end() ? it->second : std::string();
}
