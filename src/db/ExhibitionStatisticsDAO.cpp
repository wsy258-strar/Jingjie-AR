// 展馆统计 DAO 实现：预处理语句避免拼接输入，递增与读取共用同一数据库任务和连接。
#include <db/ExhibitionStatisticsDAO.h>

#include <db/DBWorkerPool.h>

#include <cstring>
#include <memory>
#include <mysql/mysql.h>
#include <mysql/mysqld_error.h>

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

bool insertViewEvent(MYSQL* connection,
                     const std::string& exhibitionId,
                     const std::string& bootstrapRequestId,
                     bool* inserted)
{
    if (!connection || !inserted) return false;
    *inserted = false;
    MYSQL_STMT* statement = mysql_stmt_init(connection);
    const char* const sql =
        "INSERT INTO exhibition_view_events "
        "(exhibition_id, bootstrap_request_id) VALUES (?, ?)";
    bool ok = statement && mysql_stmt_prepare(statement, sql, std::strlen(sql)) == 0;
    MYSQL_BIND parameters[2];
    std::memset(parameters, 0, sizeof(parameters));
    unsigned long exhibitionIdLength = static_cast<unsigned long>(exhibitionId.size());
    unsigned long requestIdLength = static_cast<unsigned long>(bootstrapRequestId.size());
    parameters[0].buffer_type = MYSQL_TYPE_STRING;
    parameters[0].buffer = const_cast<char*>(exhibitionId.data());
    parameters[0].buffer_length = exhibitionIdLength;
    parameters[0].length = &exhibitionIdLength;
    parameters[1].buffer_type = MYSQL_TYPE_STRING;
    parameters[1].buffer = const_cast<char*>(bootstrapRequestId.data());
    parameters[1].buffer_length = requestIdLength;
    parameters[1].length = &requestIdLength;
    if (ok) ok = mysql_stmt_bind_param(statement, parameters) == 0;
    if (ok)
    {
        if (mysql_stmt_execute(statement) == 0)
        {
            ok = mysql_stmt_affected_rows(statement) == 1;
            *inserted = ok;
        }
        else if (mysql_stmt_errno(statement) == ER_DUP_ENTRY)
        {
            // 唯一键冲突表示同一页面初始化已记录，不是事务失败。
            ok = true;
        }
        else
        {
            ok = false;
        }
    }
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
    const std::string& bootstrapRequestId,
    const CountCallback& callback)
{
    if (exhibitionId.empty() || exhibitionId.size() > 64 ||
        bootstrapRequestId.empty() || bootstrapRequestId.size() > 128 || !dbPool_ ||
        !dbPool_->submit([exhibitionId, bootstrapRequestId, callback](
                             std::shared_ptr<MYSQL> connection) {
            uint64_t count = 0;
            bool ok = connection && mysql_autocommit(connection.get(), 0) == 0;
            bool inserted = false;
            if (ok)
                ok = insertViewEvent(connection.get(), exhibitionId,
                                     bootstrapRequestId, &inserted);
            if (ok && inserted) ok = increment(connection.get(), exhibitionId);
            if (ok) ok = selectCount(connection.get(), exhibitionId, &count);
            if (ok && mysql_commit(connection.get()) != 0)
            {
                ok = false;
                mysql_rollback(connection.get());
            }
            else if (!ok && connection)
                mysql_rollback(connection.get());
            if (connection && mysql_autocommit(connection.get(), 1) != 0) ok = false;
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
