// 当前线程辅助接口：缓存 Linux TID，避免日志等高频路径反复触发系统调用。
#pragma once

#include <unistd.h>
#include <sys/syscall.h>
namespace CurrentThread
{
    extern thread_local int t_cachedTid; // 保存tid缓存 因为系统调用非常耗时 拿到tid后将其保存

    /// 将内核线程 ID 写入本线程的 thread_local 缓存。
    void cacheTid();

    /// 返回当前线程的 Linux TID；首次调用才执行 SYS_gettid。
    inline int tid() // 内联函数只在当前文件中起作用
    {
        if (__builtin_expect(t_cachedTid == 0, 0)) // __builtin_expect 是一种底层优化 此语句意思是如果还未获取tid 进入if 通过cacheTid()系统调用获取tid
        {
            cacheTid();
        }
        return t_cachedTid;
    }
}
