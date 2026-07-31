// 展馆统计 DAO 实现：预处理语句避免拼接输入，递增与读取共用同一数据库任务和连接。
#include <db/ExhibitionStatisticsDAO.h>

#include <db/DBWorkerPool.h>

#include <cstring>
#include <memory>
#include <mysql/mysql.h>

namespace {

bool selectCount(MYSQL* connection, const std::string& exhibitionId, uint64_t* count)
{
    if (!connection || !count) return false;
    *count = 0;

    MYSQL_STMT* statement = mysql_stmt_init(connection);
    const char* const sql =
        "SELECT total_views FROM exhibition_statistics WHERE exhibition_id = ?";
    bool ok = statement && mysql_stmt_prepare(statement, sql, std::strlen(sql)) == 0;
    MYSQL_BIND parameter;
    std::memset(&parameter, 0, sizeof(parameter));
    unsigned long exhibitionIdLength = 0;
    exhibitionIdLength = static_cast<unsigned long>(exhibitionId.size());
    parameter.buffer_type = MYSQL_TYPE_STRING;
    parameter.buffer = const_cast<char*>(exhibitionId.data());
    parameter.buffer_length = exhibitionIdLength;
    parameter.length = &exhibitionIdLength;
    if (ok) ok = mysql_stmt_bind_param(statement, &parameter) == 0;
    if (ok) ok = mysql_stmt_execute(statement) == 0;

    uint64_t value = 0;
    MYSQL_BIND result;
    std::memset(&result, 0, sizeof(result));
    result.buffer_type = MYSQL_TYPE_LONGLONG;
    result.buffer = &value;
    result.is_unsigned = 1;
    if (ok) ok = mysql_stmt_bind_result(statement, &result) == 0;
    if (ok) ok = mysql_stmt_fetch(statement) == 0;

    if (statement) mysql_stmt_close(statement);
    if (ok) *count = value;
    return ok;
}

bool increment(MYSQL* connection, const std::string& exhibitionId)
{
    if (!connection) return false;
    MYSQL_STMT* statement = mysql_stmt_init(connection);
    const char* const sql =
        "INSERT INTO exhibition_statistics (exhibition_id, total_views) "
        "VALUES (?, 1) "
        "ON DUPLICATE KEY UPDATE total_views = LAST_INSERT_ID(total_views + 1)";
    bool ok = statement && mysql_stmt_prepare(statement, sql, std::strlen(sql)) == 0;
    MYSQL_BIND parameter;
    std::memset(&parameter, 0, sizeof(parameter));
    unsigned long exhibitionIdLength = 0;
    exhibitionIdLength = static_cast<unsigned long>(exhibitionId.size());
    parameter.buffer_type = MYSQL_TYPE_STRING;
    parameter.buffer = const_cast<char*>(exhibitionId.data());
    parameter.buffer_length = exhibitionIdLength;
    parameter.length = &exhibitionIdLength;
    if (ok) ok = mysql_stmt_bind_param(statement, &parameter) == 0;
    if (ok) ok = mysql_stmt_execute(statement) == 0;
    if (statement) mysql_stmt_close(statement);
    return ok;
}

void complete(const ExhibitionStatisticsDAO::CountCallback& callback, bool ok, uint64_t count)
{
    if (callback) callback(ok, ok ? count : 0);
}

} // namespace

void ExhibitionStatisticsDAO::incrementAndRead(
    const std::string& exhibitionId,
    const CountCallback& callback)
{
    if (exhibitionId.empty() || !dbPool_ ||
        !dbPool_->submit([exhibitionId, callback](std::shared_ptr<MYSQL> connection) {
            uint64_t count = 0;
            const bool ok = connection && increment(connection.get(), exhibitionId) &&
                            selectCount(connection.get(), exhibitionId, &count);
            complete(callback, ok, count);
        }))
    {
        complete(callback, false, 0);
    }
}

void ExhibitionStatisticsDAO::read(
    const std::string& exhibitionId,
    const CountCallback& callback)
{
    if (exhibitionId.empty() || !dbPool_ ||
        !dbPool_->submit([exhibitionId, callback](std::shared_ptr<MYSQL> connection) {
            uint64_t count = 0;
            const bool ok = connection && selectCount(connection.get(), exhibitionId, &count);
            complete(callback, ok, count);
        }))
    {
        complete(callback, false, 0);
    }
}
