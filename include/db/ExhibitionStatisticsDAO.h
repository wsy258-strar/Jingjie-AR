// 展馆统计数据访问对象：在数据库工作线程中原子递增并读取总浏览量。
#pragma once

#include <base/noncopyable.h>

#include <cstdint>
#include <functional>
#include <string>

class DBWorkerPool;

class ExhibitionStatisticsDAO : noncopyable
{
public:
    typedef std::function<void(bool, uint64_t)> CountCallback;

    explicit ExhibitionStatisticsDAO(DBWorkerPool* dbPool) : dbPool_(dbPool) {}
    virtual ~ExhibitionStatisticsDAO() {}

    virtual void incrementAndRead(const std::string& exhibitionId,
                                  const CountCallback& callback);
    virtual void read(const std::string& exhibitionId, const CountCallback& callback);

private:
    DBWorkerPool* dbPool_;
};
