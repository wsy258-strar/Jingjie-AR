#include "TestSupport.h"

#include <db/DBWorkerPool.h>
#include <db/MySQLConnectionPool.h>
#include <db/SessionDAO.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iterator>
#include <mysql/mysql.h>
#include <string>

namespace {

void execute(MYSQL* connection, const std::string& sql)
{
    if (mysql_real_query(connection, sql.data(), sql.size()) != 0)
    {
        std::cerr << "MySQL query failed: " << mysql_error(connection)
                  << "\nSQL: " << sql << std::endl;
        std::exit(1);
    }
}

void applySchema(MYSQL* connection)
{
    std::ifstream input("sql/jingjie_ar_schema.sql");
    CHECK(input.good());
    const std::string schema((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    std::string::size_type start = 0;
    for (std::string::size_type delimiter = schema.find(';', start);
         delimiter != std::string::npos;
         delimiter = schema.find(';', start))
    {
        const std::string statement = schema.substr(start, delimiter - start);
        if (statement.find_first_not_of(" \t\r\n") != std::string::npos)
            execute(connection, statement);
        start = delimiter + 1;
    }
}

template<typename T>
T waitFor(std::future<T>* future)
{
    CHECK(future->wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    return future->get();
}

uint64_t create(SessionDAO* dao, uint64_t userId, const std::string& token)
{
    std::promise<uint64_t> promise;
    std::future<uint64_t> future = promise.get_future();
    dao->createSession(userId, token, "", [&promise](uint64_t id) { promise.set_value(id); });
    return waitFor(&future);
}

std::shared_ptr<Session> find(SessionDAO* dao, const std::string& token)
{
    std::promise<std::shared_ptr<Session> > promise;
    std::future<std::shared_ptr<Session> > future = promise.get_future();
    dao->findSessionByToken(token, [&promise](const std::shared_ptr<Session>& session) {
        promise.set_value(session);
    });
    return waitFor(&future);
}

bool revoke(SessionDAO* dao, const std::string& token)
{
    std::promise<bool> promise;
    std::future<bool> future = promise.get_future();
    dao->revokeSession(token, [&promise](bool ok) { promise.set_value(ok); });
    return waitFor(&future);
}

const char* environment(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

} // namespace

int main()
{
    MySQLConnectionPool::ConnInfo info;
    info.host = environment("MYSQL_HOST", "127.0.0.1");
    info.port = std::atoi(environment("MYSQL_PORT", "3306"));
    info.user = environment("MYSQL_USER", "root");
    info.passwd = environment("MYSQL_PASSWORD", "");
    info.db = environment("MYSQL_DATABASE", "");
    CHECK(!info.db.empty());

    MySQLConnectionPool connections(info, 2);
    std::shared_ptr<MYSQL> setup = connections.borrow();
    CHECK(setup.get() != 0);
    applySchema(setup.get());

    const std::string username("integration-session-status-user");
    const std::string token("integration-session-status-token");
    execute(setup.get(), "DELETE FROM sessions WHERE session_token = '" + token + "'");
    execute(setup.get(), "DELETE FROM users WHERE username = '" + username + "'");
    execute(setup.get(), "INSERT INTO users (username, passwd_hash) VALUES ('" +
                         username + "', 'integration')");
    const uint64_t userId = mysql_insert_id(setup.get());
    CHECK(userId != 0);

    {
        DBWorkerPool workers(&connections, 1);
        SessionDAO dao(&workers);
        const uint64_t sessionId = create(&dao, userId, token);
        CHECK(sessionId != 0);
        const std::shared_ptr<Session> active = find(&dao, token);
        CHECK(active && active->status == 1);
        CHECK(revoke(&dao, token));
        const std::shared_ptr<Session> inactive = find(&dao, token);
        CHECK(inactive && inactive->status == 0);
        CHECK(revoke(&dao, token));
    }

    execute(setup.get(), "DELETE FROM sessions WHERE session_token = '" + token + "'");
    execute(setup.get(), "DELETE FROM users WHERE id = " + std::to_string(userId));
    std::cout << "PASS: MySQL user session status" << std::endl;
    return 0;
}
