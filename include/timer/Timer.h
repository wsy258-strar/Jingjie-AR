// 单个定时任务的状态对象；TimerQueue 负责其创建、调度和最终释放。
#ifndef TIMER_H
#define TIMER_H

#include "noncopyable.h"
#include "Timestamp.h"
#include <functional>

/**
 * Timer用于描述一个定时器
 * 定时器回调函数，下一次超时时刻，重复定时器的时间间隔等
 */
class Timer : noncopyable
{
public:
    using TimerCallback = std::function<void()>;

    Timer(TimerCallback cb, Timestamp when, double interval)
        : callback_(move(cb)),
          expiration_(when),
          interval_(interval),
          repeat_(interval > 0.0) // 一次性定时器设置为0
    {
    }

    /// 在所属 EventLoop 线程执行回调；回调本身不应阻塞事件循环。
    void run() const 
    { 
        callback_(); 
    }

    Timestamp expiration() const  { return expiration_; }
    bool repeat() const { return repeat_; }

    // 重启定时器(如果是非重复事件则到期时间置为0)
    /// 重复任务按 interval_ 推进到下一次到期；一次性任务重置为无效时间戳。
    void restart(Timestamp now);

private:
    const TimerCallback callback_;  // 定时器回调函数
    Timestamp expiration_;          // 下一次的超时时刻
    const double interval_;         // 超时时间间隔，如果是一次性定时器，该值为0
    const bool repeat_;             // 是否重复(false 表示是一次性定时器)
};

#endif // TIMER_H
