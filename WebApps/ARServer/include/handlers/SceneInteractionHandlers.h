// 场景互动端点处理器：处理点赞、取消点赞和评论查询/发布的请求边界校验。
#pragma once

#include <http/AsyncResponder.h>
#include <http/HttpRequest.h>

namespace ar {

class SceneInteractionService;

class SceneInteractionHandlers
{
public:
    explicit SceneInteractionHandlers(SceneInteractionService* service) : service_(service) {}

    void detail(const HttpRequest& request, const AsyncResponder& responder) const;
    void like(const HttpRequest& request, const AsyncResponder& responder) const;
    void unlike(const HttpRequest& request, const AsyncResponder& responder) const;
    void comments(const HttpRequest& request, const AsyncResponder& responder) const;
    void comment(const HttpRequest& request, const AsyncResponder& responder) const;

private:
    SceneInteractionService* service_;
};

} // namespace ar
