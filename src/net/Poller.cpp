// Poller 公共状态实现：记录当前事件循环已注册的 Channel。
#include <Poller.h>
#include <Channel.h>

Poller::Poller(EventLoop *loop)
    : ownerLoop_(loop)
{
}

bool Poller::hasChannel(Channel *channel) const
{
    auto it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}
