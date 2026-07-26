#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <mysql/mysql.h>
#include "../base/noncopyable.h"

/**
 * @brief 用户信息数据结构
 */
struct User
{
    uint64_t    id = 0;
    std::string username;
    std::string passwdHash;
    std::string createdAt;

    User() = default;
    User(uint64_t i, std::string u, std::string p, std::string c = "")
        : id(i), username(std::move(u)), passwdHash(std::move(p)), createdAt(std::move(c)) {}
};

/**
 * @brief AR 协同会话数据结构
 */
struct Session
{
    uint64_t    id = 0;
    std::string sessionToken;
    uint64_t    userId = 0;
    std::string sceneId;
    int         status = 1;
    std::string createdAt;
    std::string updatedAt;

    Session() = default;
    Session(uint64_t i, std::string t, uint64_t u, std::string sc, int st,
            std::string ca = "", std::string ua = "")
        : id(i), sessionToken(std::move(t)), userId(u), sceneId(std::move(sc)),
          status(st), createdAt(std::move(ca)), updatedAt(std::move(ua)) {}
};

/**
 * @brief 用户 & 会话数据访问层 (DAO)：Data Access Object（数据访问对象）层
 *
 * 通过 DBWorkerPool 异步执行，不阻塞 EventLoop。
 * 每个方法接收一个回调，在 DB 操作完成后调用。
 *
 * 预处理语句防 SQL 注入，所有用户输入都通过参数绑定。
 */
class SessionDAO : noncopyable
{
public:
    typedef std::function<void(const std::shared_ptr<User>&)> UserCallback;
    typedef std::function<void(const std::shared_ptr<Session>&)> SessionCallback;
    typedef std::function<void(bool)> BoolCallback;
    typedef std::function<void(uint64_t)> IdCallback;
    typedef std::function<void(std::vector<Session>)> SessionsCallback;

    explicit SessionDAO(class DBWorkerPool* dbPool)
        : dbPool_(dbPool) {}

    // ========== 用户 ==========

    void findUserById(uint64_t userId,
                      const UserCallback& callback);

    void findUserByUsername(const std::string& username,
                            const UserCallback& callback);

    /// 异步创建用户。成功回调 id，失败回调 0。
    void createUser(const std::string& username,
                    const std::string& passwdHash,
                    const IdCallback& callback);

    // ========== 会话 ==========

    void findSessionByToken(const std::string& token,
                            const SessionCallback& callback);

    /// 创建新会话。成功回调 id，失败回调 0
    void createSession(uint64_t userId,
                       const std::string& token,
                       const std::string& sceneId,
                       const IdCallback& callback);

    /// 更新会话场景 ID
    void updateSessionScene(uint64_t sessionId,
                            const std::string& sceneId,
                            const BoolCallback& callback);

    /// 结束会话
    void endSession(uint64_t sessionId,
                    const BoolCallback& callback);

    /// 获取用户的所有活跃会话
    void findActiveSessionsByUser(uint64_t userId,
                                  const SessionsCallback& callback);

private:
    DBWorkerPool* dbPool_;
};
