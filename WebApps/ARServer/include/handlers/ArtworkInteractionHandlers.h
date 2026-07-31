// 作品互动 HTTP 处理器：将路由参数、鉴权属性和 JSON 请求映射为业务服务调用。
#pragma once

#include <http/AsyncResponder.h>
#include <http/HttpRequest.h>

namespace ar {

class ArtworkInteractionService;

class ArtworkInteractionHandlers
{
public:
    explicit ArtworkInteractionHandlers(ArtworkInteractionService* service) : service_(service) {}

    void detail(const HttpRequest& request, const AsyncResponder& responder) const;
    void like(const HttpRequest& request, const AsyncResponder& responder) const;
    void unlike(const HttpRequest& request, const AsyncResponder& responder) const;
    void comments(const HttpRequest& request, const AsyncResponder& responder) const;
    void comment(const HttpRequest& request, const AsyncResponder& responder) const;

private:
    ArtworkInteractionService* service_;
};

} // namespace ar
