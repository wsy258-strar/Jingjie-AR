#pragma once

#include <http/HttpResponse.h>

#include <atomic>
#include <functional>
#include <memory>

class AsyncResponder
{
public:
    typedef std::function<void(HttpResponse)> Sender;

    AsyncResponder();
    explicit AsyncResponder(const Sender& sender);

    bool send(const HttpResponse& response) const;
    bool valid() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};
