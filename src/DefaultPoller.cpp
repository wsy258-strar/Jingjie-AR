// 默认 Poller 工厂：Linux 平台选择 EPollPoller 作为 I/O 多路复用实现。
#include <stdlib.h>

#include <Poller.h>
#include <EPollPoller.h>

Poller *Poller::newDefaultPoller(EventLoop *loop)
{
    if (::getenv("MUDUO_USE_POLL"))
    {
        return nullptr; // 生成poll的实例
    }
    else
    {
        return new EPollPoller(loop); // 生成epoll的实例
    }
}
