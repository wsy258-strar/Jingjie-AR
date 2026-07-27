// 禁止拷贝的基类，用于拥有唯一线程、文件描述符或事件循环状态的对象。
#pragma once // 防止头文件重复包含

/**
 * noncopyable被继承后 派生类对象可正常构造和析构 但派生类对象无法进行拷贝构造和赋值构造
 **/
class noncopyable
{
public:
    noncopyable(const noncopyable &) = delete;
    noncopyable &operator=(const noncopyable &) = delete;
    // void operator=(const noncopyable &) = delete;    // muduo将返回值变为void 这其实无可厚非
protected:
    noncopyable() = default;
    ~noncopyable() = default;
};
