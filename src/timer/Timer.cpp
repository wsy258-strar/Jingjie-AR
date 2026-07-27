// Timer 到期后的重启规则实现。
#include <Timer.h>

void Timer::restart(Timestamp now)
{
    // 以本次处理时刻为基准，避免长时间回调导致重复任务连续补触发。
    if (repeat_)
    {
        // 如果是重复定时事件，则继续添加定时事件，得到新事件到期事件
        expiration_ = addTime(now, interval_);
    }
    else 
    {
        expiration_ = Timestamp();
    }
}
