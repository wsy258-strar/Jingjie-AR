// 固定槽位内存池接口：适合大小不超过 MAX_SLOT_SIZE 的短生命周期对象。
#pragma once 

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>

namespace memoryPool
{
#define MEMORY_POOL_NUM 64
#define SLOT_BASE_SIZE 8
#define MAX_SLOT_SIZE 512


/* 具体内存池的槽大小没法确定，因为每个内存池的槽大小不同(8的倍数)
   所以这个槽结构体的sizeof 不是实际的槽大小 */
struct Slot 
{
    Slot* next;
};

class MemoryPool
{
public:
    /**
     * 创建一个内存池；BlockSize 表示一次向系统申请的原始内存块大小。
     * 槽位大小由 init() 在该池投入使用前确定。
     */
    MemoryPool(size_t BlockSize = 4096);
    ~MemoryPool();
    
    /// 初始化槽位大小；同一池初始化后不得更改，否则既有槽位布局将失效。
    void init(size_t);

    void* allocate();
    void deallocate(void*);
private:
    void allocateNewBlock();
    size_t padPointer(char* p, size_t align);

private:
    int        BlockSize_; // 内存块大小
    int        SlotSize_; // 槽大小
    Slot*      firstBlock_; // 指向内存池管理的首个实际内存块
    Slot*      curSlot_; // 指向当前未被使用过的槽
    Slot*      freeList_; // 指向空闲的槽(被使用过后又被释放的槽)
    Slot*      lastSlot_; // 作为当前内存块中最后能够存放元素的位置标识(超过该位置需申请新的内存块)
    std::mutex mutexForFreeList_; // 保证freeList_在多线程中操作的原子性
    std::mutex mutexForBlock_; // 保证多线程情况下避免不必要的重复开辟内存导致的浪费行为
};


class HashBucket
{
public:
    /// 初始化所有按 8 字节递增的槽位池；保留该接口以兼容旧调用方。
    static void initMemoryPool();
    static MemoryPool& getMemoryPool(int index);

    /// 根据对象大小选择槽位池；超过上限的对象直接交给全局 operator new。
    static void* useMemory(size_t size)
    {
        if (size <= 0)
            return nullptr;
        if (size > MAX_SLOT_SIZE) // 大于512字节的内存，则使用new
            return operator new(size);

        // 相当于size / 8 向上取整（因为分配内存只能大不能小
        return getMemoryPool(((size + 7) / SLOT_BASE_SIZE) - 1).allocate();
    }

    /// 按原始对象大小将内存归还给对应池，调用方必须传入与分配时一致的大小。
    static void freeMemory(void* ptr, size_t size)
    {
        if (!ptr)
            return;
        if (size > MAX_SLOT_SIZE)
        {
            operator delete(ptr);
            return;
        }

        getMemoryPool(((size + 7) / SLOT_BASE_SIZE) - 1).deallocate(ptr);
    }

    template<typename T, typename... Args> 
    friend T* newElement(Args&&... args);
    
    template<typename T>
    friend void deleteElement(T* p);
};

template<typename T, typename... Args>
T* newElement(Args&&... args)
{
    // 分配与构造分离：池只管理原始存储，placement new 负责建立对象生命周期。
    T* p = nullptr;
    // 根据元素大小选取合适的内存池分配内存
    if ((p = reinterpret_cast<T*>(HashBucket::useMemory(sizeof(T)))) != nullptr)
        // 在分配的内存上构造对象
        new(p) T(std::forward<Args>(args)...); //完美转发，保证传递给 T 构造函数的参数 “左值还是左值、右值还是右值”（比如传递临时字符串时，保持右值特性，触发 T 的移动构造，减少拷贝开销）。

    return p;
}

template<typename T>
void deleteElement(T* p)
{
    // 必须先结束对象生命周期，再把原始存储放回可复用空闲链表。
    // 对象析构
    if (p)
    {
        p->~T();
         // 内存回收
        HashBucket::freeMemory(reinterpret_cast<void*>(p), sizeof(T));
    }
}

} // namespace memoryPool
