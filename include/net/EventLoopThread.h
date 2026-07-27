// 在独立线程中创建并运行一个 EventLoop，并向调用线程安全发布该循环指针。
#pragma once

#include <functional>
#include <mutex>
#include <condition_variable>
#include <string>

#include "noncopyable.h"
#include "Thread.h"

class EventLoop;
//提供Thread类创建线程后，线程的运行函数threadfunc
class EventLoopThread : noncopyable
{
public:
    /// 启动线程并等待 EventLoop 初始化完成后返回；返回指针由该线程管理。
    using ThreadInitCallback = std::function<void(EventLoop *)>;

    EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback(),
                    const std::string &name = std::string());
    ~EventLoopThread();

    EventLoop *startLoop();

private:
    void threadFunc();

    EventLoop *loop_;
    bool exiting_;
    Thread thread_;
    std::mutex mutex_;             // 互斥锁
    std::condition_variable cond_; // 条件变量
    ThreadInitCallback callback_;
};
