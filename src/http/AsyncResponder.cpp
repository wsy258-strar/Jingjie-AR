// AsyncResponder 共享一次性完成状态的实现。
#include <http/AsyncResponder.h>

struct AsyncResponder::State
{
    explicit State(const Sender& value)
        : sender(value),
          completed(false)
    {
    }

    Sender sender;
    std::atomic_bool completed;
};

AsyncResponder::AsyncResponder()
{
}

AsyncResponder::AsyncResponder(const Sender& sender)
    : state_(new State(sender))
{
}

bool AsyncResponder::send(const HttpResponse& response) const
{
    if (!state_ || !state_->sender || state_->completed.exchange(true))
    {
        return false;
    }
    state_->sender(response);
    return true;
}

bool AsyncResponder::valid() const
{
    return state_ && state_->sender;
}
