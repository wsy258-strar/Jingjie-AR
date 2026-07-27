// 一次性异步 HTTP 响应句柄：允许后台任务在请求回调返回后安全地完成响应。
#pragma once

#include <http/HttpResponse.h>

#include <atomic>
#include <functional>
#include <memory>

class AsyncResponder
{
public:
    /// send 仅首次成功；共享状态防止多个完成路径重复写同一连接。
    typedef std::function<void(HttpResponse)> Sender;

    AsyncResponder();
    explicit AsyncResponder(const Sender& sender);

    bool send(const HttpResponse& response) const;
    bool valid() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};
