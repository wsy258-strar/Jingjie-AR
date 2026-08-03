// 作品互动 DAO：所有用户输入均通过 MySQL 预处理语句绑定，数据库工作不阻塞 EventLoop。
#include <db/ArtworkInteractionDAO.h>

#include <db/DBWorkerPool.h>

#include <cstring>
#include <memory>
#include <mysql/mysql.h>
#include <mysql/mysqld_error.h>

namespace {

bool querySummary(MYSQL* connection, const std::string& artworkId, uint64_t userId,
                  uint64_t* count, bool* liked, uint64_t* commentCount)
{
    MYSQL_STMT* statement = mysql_stmt_init(connection);
    if (!statement) return false;
    const char* sql =
        "SELECT "
        "(SELECT COUNT(*) FROM artwork_likes WHERE artwork_id = ?), "
        "EXISTS(SELECT 1 FROM artwork_likes WHERE artwork_id = ? AND user_id = ?), "
        "(SELECT COUNT(*) FROM artwork_comments WHERE artwork_id = ?)";
    bool ok = mysql_stmt_prepare(statement, sql, std::strlen(sql)) == 0;

    MYSQL_BIND parameters[4];
    std::memset(parameters, 0, sizeof(parameters));
    unsigned long artworkLength = static_cast<unsigned long>(artworkId.size());
    uint64_t user = userId;
    parameters[0].buffer_type = MYSQL_TYPE_STRING;
    parameters[0].buffer = const_cast<char*>(artworkId.c_str());
    parameters[0].buffer_length = artworkLength;
    parameters[0].length = &artworkLength;
    parameters[1].buffer_type = MYSQL_TYPE_STRING;
    parameters[1].buffer = const_cast<char*>(artworkId.c_str());
    parameters[1].buffer_length = artworkLength;
    parameters[1].length = &artworkLength;
    parameters[2].buffer_type = MYSQL_TYPE_LONGLONG;
    parameters[2].buffer = &user;
    parameters[2].is_unsigned = 1;
    parameters[3].buffer_type = MYSQL_TYPE_STRING;
    parameters[3].buffer = const_cast<char*>(artworkId.c_str());
    parameters[3].buffer_length = artworkLength;
    parameters[3].length = &artworkLength;

    uint64_t countValue = 0;
    unsigned char likedValue = 0;
    uint64_t commentCountValue = 0;
    MYSQL_BIND results[3];
    std::memset(results, 0, sizeof(results));
    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &countValue;
    results[0].is_unsigned = 1;
    results[1].buffer_type = MYSQL_TYPE_TINY;
    results[1].buffer = &likedValue;
    results[1].is_unsigned = 1;
    results[2].buffer_type = MYSQL_TYPE_LONGLONG;
    results[2].buffer = &commentCountValue;
    results[2].is_unsigned = 1;

    if (ok) ok = mysql_stmt_bind_param(statement, parameters) == 0;
    if (ok) ok = mysql_stmt_execute(statement) == 0;
    if (ok) ok = mysql_stmt_bind_result(statement, results) == 0;
    if (ok) ok = mysql_stmt_fetch(statement) == 0;
    mysql_stmt_close(statement);
    if (ok)
    {
        if (count) *count = countValue;
        if (liked) *liked = likedValue != 0;
        if (commentCount) *commentCount = commentCountValue;
    }
    return ok;
}

void changeLike(DBWorkerPool* pool, const char* sql, bool duplicateIsSuccess,
                const std::string& artworkId, uint64_t userId,
                const ArtworkInteractionDAO::LikeCallback& callback)
{
    if (!pool || !pool->submit(
        [sql, duplicateIsSuccess, artworkId, userId, callback](std::shared_ptr<MYSQL> connection) {
            if (!connection)
            {
                if (callback) callback(false, false, 0, false);
                return;
            }
            MYSQL_STMT* statement = mysql_stmt_init(connection.get());
            bool ok = statement && mysql_stmt_prepare(statement, sql, std::strlen(sql)) == 0;
            MYSQL_BIND parameters[2];
            std::memset(parameters, 0, sizeof(parameters));
            unsigned long artworkLength = static_cast<unsigned long>(artworkId.size());
            uint64_t user = userId;
            parameters[0].buffer_type = MYSQL_TYPE_STRING;
            parameters[0].buffer = const_cast<char*>(artworkId.c_str());
            parameters[0].buffer_length = artworkLength;
            parameters[0].length = &artworkLength;
            parameters[1].buffer_type = MYSQL_TYPE_LONGLONG;
            parameters[1].buffer = &user;
            parameters[1].is_unsigned = 1;
            if (ok) ok = mysql_stmt_bind_param(statement, parameters) == 0;
            bool executed = false;
            if (ok && mysql_stmt_execute(statement) == 0)
                executed = true;
            else if (ok)
            {
                const unsigned int error = mysql_stmt_errno(statement);
                ok = duplicateIsSuccess && error == ER_DUP_ENTRY;
            }
            const bool changed = ok && executed && mysql_stmt_affected_rows(statement) > 0;
            if (statement) mysql_stmt_close(statement);

            uint64_t count = 0;
            bool liked = false;
            if (ok) ok = querySummary(connection.get(), artworkId, userId, &count, &liked, 0);
            if (callback) callback(ok, changed, ok ? count : 0, ok && liked);
        }))
    {
        if (callback) callback(false, false, 0, false);
    }
}

} // namespace

void ArtworkInteractionDAO::like(const std::string& artworkId, uint64_t userId,
                                 const LikeCallback& callback)
{
    changeLike(dbPool_,
               "INSERT INTO artwork_likes (artwork_id, user_id) VALUES (?, ?)", true,
               artworkId, userId, callback);
}

void ArtworkInteractionDAO::unlike(const std::string& artworkId, uint64_t userId,
                                   const LikeCallback& callback)
{
    changeLike(dbPool_,
               "DELETE FROM artwork_likes WHERE artwork_id = ? AND user_id = ?", false,
               artworkId, userId, callback);
}

void ArtworkInteractionDAO::summary(const std::string& artworkId, uint64_t optionalUserId,
                                    const SummaryCallback& callback)
{
    if (!dbPool_ || !dbPool_->submit(
        [artworkId, optionalUserId, callback](std::shared_ptr<MYSQL> connection) {
            uint64_t count = 0;
            bool liked = false;
            uint64_t commentCount = 0;
            const bool ok = connection &&
                querySummary(connection.get(), artworkId, optionalUserId, &count, &liked,
                             &commentCount);
            if (callback) callback(ok, ok ? count : 0, ok && liked, ok ? commentCount : 0);
        }))
    {
        if (callback) callback(false, 0, false, 0);
    }
}

void ArtworkInteractionDAO::createComment(const std::string& artworkId, uint64_t userId,
                                          const std::string& content,
                                          const CommentCallback& callback)
{
    if (!dbPool_ || !dbPool_->submit(
        [artworkId, userId, content, callback](std::shared_ptr<MYSQL> connection) {
            if (!connection)
            {
                if (callback) callback(false, 0);
                return;
            }
            MYSQL_STMT* statement = mysql_stmt_init(connection.get());
            const char* sql =
                "INSERT INTO artwork_comments (artwork_id, user_id, content) VALUES (?, ?, ?)";
            bool ok = statement && mysql_stmt_prepare(statement, sql, std::strlen(sql)) == 0;
            MYSQL_BIND parameters[3];
            std::memset(parameters, 0, sizeof(parameters));
            unsigned long artworkLength = static_cast<unsigned long>(artworkId.size());
            unsigned long contentLength = static_cast<unsigned long>(content.size());
            uint64_t user = userId;
            parameters[0].buffer_type = MYSQL_TYPE_STRING;
            parameters[0].buffer = const_cast<char*>(artworkId.c_str());
            parameters[0].buffer_length = artworkLength;
            parameters[0].length = &artworkLength;
            parameters[1].buffer_type = MYSQL_TYPE_LONGLONG;
            parameters[1].buffer = &user;
            parameters[1].is_unsigned = 1;
            parameters[2].buffer_type = MYSQL_TYPE_STRING;
            parameters[2].buffer = const_cast<char*>(content.c_str());
            parameters[2].buffer_length = contentLength;
            parameters[2].length = &contentLength;
            if (ok) ok = mysql_stmt_bind_param(statement, parameters) == 0;
            if (ok) ok = mysql_stmt_execute(statement) == 0;
            const uint64_t id = ok ? mysql_stmt_insert_id(statement) : 0;
            if (statement) mysql_stmt_close(statement);
            if (callback) callback(ok, ok ? id : 0);
        }))
    {
        if (callback) callback(false, 0);
    }
}

void ArtworkInteractionDAO::listComments(const std::string& artworkId, uint64_t beforeId,
                                         uint32_t limit, const CommentsCallback& callback)
{
    if (!dbPool_ || !dbPool_->submit(
        [artworkId, beforeId, limit, callback](std::shared_ptr<MYSQL> connection) {
            std::vector<ArtworkComment> comments;
            uint64_t nextBefore = 0;
            if (!connection)
            {
                if (callback) callback(false, comments, nextBefore);
                return;
            }
            MYSQL_STMT* statement = mysql_stmt_init(connection.get());
            const char* sql =
                "SELECT c.id, u.username, c.content "
                "FROM artwork_comments c JOIN users u ON u.id = c.user_id "
                "WHERE c.artwork_id = ? AND c.id < ? "
                "ORDER BY c.id DESC LIMIT ?";
            bool ok = statement && mysql_stmt_prepare(statement, sql, std::strlen(sql)) == 0;
            MYSQL_BIND parameters[3];
            std::memset(parameters, 0, sizeof(parameters));
            unsigned long artworkLength = static_cast<unsigned long>(artworkId.size());
            uint64_t cursor = beforeId ? beforeId : UINT64_MAX;
            uint32_t rowLimit = limit > 20 ? 20 : (limit ? limit : 20);
            parameters[0].buffer_type = MYSQL_TYPE_STRING;
            parameters[0].buffer = const_cast<char*>(artworkId.c_str());
            parameters[0].buffer_length = artworkLength;
            parameters[0].length = &artworkLength;
            parameters[1].buffer_type = MYSQL_TYPE_LONGLONG;
            parameters[1].buffer = &cursor;
            parameters[1].is_unsigned = 1;
            parameters[2].buffer_type = MYSQL_TYPE_LONG;
            parameters[2].buffer = &rowLimit;
            parameters[2].is_unsigned = 1;
            if (ok) ok = mysql_stmt_bind_param(statement, parameters) == 0;
            if (ok) ok = mysql_stmt_execute(statement) == 0;

            uint64_t id = 0;
            // utf8mb4 的 VARCHAR 长度按字符计；缓冲区按最坏四字节字符预留。
            char username[257] = {0};
            char content[4001] = {0};
            unsigned long usernameLength = 0;
            unsigned long contentLength = 0;
            MYSQL_BIND results[3];
            std::memset(results, 0, sizeof(results));
            results[0].buffer_type = MYSQL_TYPE_LONGLONG;
            results[0].buffer = &id;
            results[0].is_unsigned = 1;
            results[1].buffer_type = MYSQL_TYPE_STRING;
            results[1].buffer = username;
            results[1].buffer_length = sizeof(username);
            results[1].length = &usernameLength;
            results[2].buffer_type = MYSQL_TYPE_STRING;
            results[2].buffer = content;
            results[2].buffer_length = sizeof(content);
            results[2].length = &contentLength;
            if (ok) ok = mysql_stmt_bind_result(statement, results) == 0;
            while (ok)
            {
                const int fetch = mysql_stmt_fetch(statement);
                if (fetch == MYSQL_NO_DATA) break;
                if (fetch != 0)
                {
                    ok = false;
                    break;
                }
                ArtworkComment comment;
                comment.id = id;
                comment.username.assign(username, usernameLength);
                comment.content.assign(content, contentLength);
                comments.push_back(comment);
                nextBefore = id;
            }
            if (statement) mysql_stmt_close(statement);
            if (!ok)
            {
                comments.clear();
                nextBefore = 0;
            }
            if (callback) callback(ok, comments, nextBefore);
        }))
    {
        if (callback) callback(false, std::vector<ArtworkComment>(), 0);
    }
}
