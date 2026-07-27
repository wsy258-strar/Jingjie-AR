// 缓存淘汰策略的最小抽象，供不同缓存实现以统一方式读写键值。
#pragma once

namespace cache
{

template <typename Key, typename Value>
class KICachePolicy
{
public:
    /// 虚析构保证通过策略基类指针销毁具体缓存时能够正确释放资源。
    virtual ~KICachePolicy() {};

    // 添加缓存接口
    /// 写入或覆盖键值；具体策略负责容量控制与淘汰。
    virtual void put(Key key, Value value) = 0;

    // key是传入参数  访问到的值以传出参数的形式返回 | 访问成功返回true
    /// 查找键并通过 value 输出命中值；命中返回 true。
    virtual bool get(Key key, Value& value) = 0;
    // 如果缓存中能找到key，则直接返回value
    virtual Value get(Key key) = 0;

};

} // namespace cache
