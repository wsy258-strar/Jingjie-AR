#include <db/SceneInteractionDAO.h>

#include <db/DBWorkerPool.h>

#include <cstring>
#include <memory>
#include <mysql/mysql.h>

namespace {

bool likeCount(MYSQL* connection, const std::string& sceneId, uint64_t* count)
{
    MYSQL_STMT* statement = mysql_stmt_init(connection);
    if (!statement) return false;
    const char* sql = "SELECT COUNT(*) FROM scene_likes WHERE scene_id = ?";
    bool ok = mysql_stmt_prepare(statement, sql, std::strlen(sql)) == 0;
    unsigned long sceneLength = static_cast<unsigned long>(sceneId.size());
    MYSQL_BIND param;
    std::memset(&param, 0, sizeof(param));
    param.buffer_type = MYSQL_TYPE_STRING;
    param.buffer = const_cast<char*>(sceneId.c_str());
    param.buffer_length = sceneLength;
    param.length = &sceneLength;
    uint64_t value = 0;
    MYSQL_BIND result;
    std::memset(&result, 0, sizeof(result));
    result.buffer_type = MYSQL_TYPE_LONGLONG;
    result.buffer = &value;
    if (ok) ok = mysql_stmt_bind_param(statement, &param) == 0;
    if (ok) ok = mysql_stmt_execute(statement) == 0;
    if (ok) ok = mysql_stmt_bind_result(statement, &result) == 0;
    if (ok) ok = mysql_stmt_fetch(statement) == 0;
    mysql_stmt_close(statement);
    if (ok && count) *count = value;
    return ok;
}

void changeLike(DBWorkerPool* pool, const std::string& sql, const std::string& sceneId,
                uint64_t userId, const SceneInteractionDAO::LikeCallback& callback)
{
    if (!pool || !pool->submit([sql, sceneId, userId, callback](std::shared_ptr<MYSQL> connection) {
        if (!connection) { if (callback) callback(false, false, 0); return; }
        MYSQL_STMT* statement = mysql_stmt_init(connection.get());
        if (!statement) { if (callback) callback(false, false, 0); return; }
        bool ok = mysql_stmt_prepare(statement, sql.c_str(), sql.size()) == 0;
        MYSQL_BIND bind[2];
        std::memset(bind, 0, sizeof(bind));
        unsigned long sceneLength = static_cast<unsigned long>(sceneId.size());
        uint64_t user = userId;
        bind[0].buffer_type = MYSQL_TYPE_STRING;
        bind[0].buffer = const_cast<char*>(sceneId.c_str());
        bind[0].buffer_length = sceneLength;
        bind[0].length = &sceneLength;
        bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[1].buffer = &user;
        bind[1].is_unsigned = 1;
        if (ok) ok = mysql_stmt_bind_param(statement, bind) == 0;
        if (ok) ok = mysql_stmt_execute(statement) == 0;
        const bool changed = ok && mysql_stmt_affected_rows(statement) > 0;
        mysql_stmt_close(statement);
        uint64_t count = 0;
        if (ok) ok = likeCount(connection.get(), sceneId, &count);
        if (callback) callback(ok, changed, ok ? count : 0);
    }))
    {
        if (callback) callback(false, false, 0);
    }
}

} // namespace

void SceneInteractionDAO::like(const std::string& sceneId, uint64_t userId, const LikeCallback& callback)
{
    changeLike(dbPool_, "INSERT IGNORE INTO scene_likes (scene_id, user_id) VALUES (?, ?)",
               sceneId, userId, callback);
}

void SceneInteractionDAO::unlike(const std::string& sceneId, uint64_t userId, const LikeCallback& callback)
{
    changeLike(dbPool_, "DELETE FROM scene_likes WHERE scene_id = ? AND user_id = ?",
               sceneId, userId, callback);
}

void SceneInteractionDAO::createComment(const std::string& sceneId, uint64_t userId,
                                        const std::string& content, const CommentCallback& callback)
{
    if (!dbPool_ || !dbPool_->submit([sceneId, userId, content, callback](std::shared_ptr<MYSQL> connection) {
        if (!connection) { if (callback) callback(false, 0); return; }
        MYSQL_STMT* statement = mysql_stmt_init(connection.get());
        const char* sql = "INSERT INTO scene_comments (scene_id, user_id, content) VALUES (?, ?, ?)";
        bool ok = statement && mysql_stmt_prepare(statement, sql, std::strlen(sql)) == 0;
        MYSQL_BIND bind[3]; std::memset(bind, 0, sizeof(bind));
        unsigned long sceneLength = static_cast<unsigned long>(sceneId.size());
        unsigned long contentLength = static_cast<unsigned long>(content.size());
        uint64_t user = userId;
        bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = const_cast<char*>(sceneId.c_str()); bind[0].length = &sceneLength;
        bind[1].buffer_type = MYSQL_TYPE_LONGLONG; bind[1].buffer = &user;
        bind[2].buffer_type = MYSQL_TYPE_STRING; bind[2].buffer = const_cast<char*>(content.c_str()); bind[2].length = &contentLength;
        if (ok) ok = mysql_stmt_bind_param(statement, bind) == 0;
        if (ok) ok = mysql_stmt_execute(statement) == 0;
        const uint64_t id = ok ? mysql_stmt_insert_id(statement) : 0;
        if (statement) mysql_stmt_close(statement);
        if (callback) callback(ok, id);
    })) { if (callback) callback(false, 0); }
}

void SceneInteractionDAO::summary(const std::string& sceneId, const SummaryCallback& callback)
{
    if (!dbPool_ || !dbPool_->submit([sceneId, callback](std::shared_ptr<MYSQL> connection) {
        uint64_t count = 0;
        const bool ok = connection && likeCount(connection.get(), sceneId, &count);
        if (callback) callback(ok, ok ? count : 0);
    })) { if (callback) callback(false, 0); }
}

void SceneInteractionDAO::listComments(const std::string& sceneId, uint64_t beforeId, uint32_t limit,
                                       const CommentsCallback& callback)
{
    if (!dbPool_ || !dbPool_->submit([sceneId, beforeId, limit, callback](std::shared_ptr<MYSQL> connection) {
        std::vector<SceneComment> comments;
        uint64_t nextBefore = 0;
        if (!connection) { if (callback) callback(false, comments, nextBefore); return; }
        MYSQL_STMT* statement = mysql_stmt_init(connection.get());
        const char* sql = "SELECT c.id, u.username, c.content FROM scene_comments c "
                          "JOIN users u ON u.id = c.user_id "
                          "WHERE c.scene_id = ? AND c.id < ? ORDER BY c.id DESC LIMIT ?";
        bool ok = statement && mysql_stmt_prepare(statement, sql, std::strlen(sql)) == 0;
        MYSQL_BIND bind[3]; std::memset(bind, 0, sizeof(bind));
        unsigned long sceneLength = static_cast<unsigned long>(sceneId.size());
        uint64_t cursor = beforeId ? beforeId : UINT64_MAX;
        unsigned long rowLimit = limit > 20 ? 20 : (limit ? limit : 20);
        bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = const_cast<char*>(sceneId.c_str()); bind[0].length = &sceneLength;
        bind[1].buffer_type = MYSQL_TYPE_LONGLONG; bind[1].buffer = &cursor;
        bind[1].is_unsigned = 1;
        bind[2].buffer_type = MYSQL_TYPE_LONG; bind[2].buffer = &rowLimit;
        if (ok) ok = mysql_stmt_bind_param(statement, bind) == 0;
        if (ok) ok = mysql_stmt_execute(statement) == 0;
        uint64_t id = 0; char username[256] = {0}; char content[1024] = {0};
        unsigned long usernameLength = 0, contentLength = 0;
        MYSQL_BIND result[3]; std::memset(result, 0, sizeof(result));
        result[0].buffer_type = MYSQL_TYPE_LONGLONG; result[0].buffer = &id;
        result[0].is_unsigned = 1;
        result[1].buffer_type = MYSQL_TYPE_STRING; result[1].buffer = username; result[1].buffer_length = sizeof(username); result[1].length = &usernameLength;
        result[2].buffer_type = MYSQL_TYPE_STRING; result[2].buffer = content; result[2].buffer_length = sizeof(content); result[2].length = &contentLength;
        if (ok) ok = mysql_stmt_bind_result(statement, result) == 0;
        while (ok && mysql_stmt_fetch(statement) == 0)
        {
            SceneComment comment; comment.id = id;
            comment.username.assign(username, usernameLength);
            comment.content.assign(content, contentLength);
            comments.push_back(comment);
            nextBefore = id;
        }
        if (statement) mysql_stmt_close(statement);
        if (!ok) { comments.clear(); nextBefore = 0; }
        if (callback) callback(ok, comments, nextBefore);
    })) { if (callback) callback(false, std::vector<SceneComment>(), 0); }
}
