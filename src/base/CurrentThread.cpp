// CurrentThread 的线程本地缓存实现。
#include <CurrentThread.h>

namespace CurrentThread
{
      thread_local int t_cachedTid = 0; // 在源文件中定义线程局部变量
    void cacheTid()
    {
        // 每个线程独立缓存；零值表示尚未读取过内核 TID。
        if (t_cachedTid == 0)
        {
            t_cachedTid = static_cast<pid_t>(::syscall(SYS_gettid)); // Ensure syscall and SYS_gettid are defined
        }
    }
}
