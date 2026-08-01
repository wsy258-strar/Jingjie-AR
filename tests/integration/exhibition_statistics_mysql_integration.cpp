#include "TestSupport.h"

#include <db/DBWorkerPool.h>
#include <db/ExhibitionStatisticsDAO.h>
#include <db/MySQLConnectionPool.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iterator>
#include <mysql/mysql.h>
#include <string>
#include <vector>

namespace {

struct CountResult
{
    bool ok;
    uint64_t count;
};

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

std::future<CountResult> increment(ExhibitionStatisticsDAO* dao,
                                   const std::string& exhibitionId,
                                   const std::string& bootstrapRequestId,
                                   const std::shared_ptr<std::promise<CountResult> >& promise,
                                   const std::shared_ptr<std::atomic<int> >& callbacks)
{
    std::future<CountResult> future = promise->get_future();
    dao->incrementAndRead(
        exhibitionId, bootstrapRequestId,
        [promise, callbacks](bool ok, uint64_t count) {
            CHECK(callbacks->fetch_add(1) == 0);
            CountResult result = {ok, count};
            promise->set_value(result);
        });
    return future;
}

CountResult waitFor(std::future<CountResult>* future)
{
    CHECK(future->wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    return future->get();
}

uint64_t scalar(MYSQL* connection, const std::string& sql)
{
    execute(connection, sql);
    MYSQL_RES* result = mysql_store_result(connection);
    CHECK(result != 0);
    MYSQL_ROW row = mysql_fetch_row(result);
    CHECK(row != 0 && row[0] != 0);
    const uint64_t value = static_cast<uint64_t>(std::strtoull(row[0], 0, 10));
    mysql_free_result(result);
    return value;
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

    MySQLConnectionPool connections(info, 4);
    std::shared_ptr<MYSQL> setup = connections.borrow();
    CHECK(setup.get() != 0);
    applySchema(setup.get());

    const std::string exhibitionId("integration-exhibition-statistics");
    execute(setup.get(), "DELETE FROM exhibition_view_events WHERE exhibition_id = '" +
                         exhibitionId + "'");
    execute(setup.get(), "DELETE FROM exhibition_statistics WHERE exhibition_id = '" +
                         exhibitionId + "'");

    {
        DBWorkerPool workers(&connections, 4);
        ExhibitionStatisticsDAO dao(&workers);

        execute(setup.get(), "DROP TRIGGER IF EXISTS integration_view_event_failure");
        execute(setup.get(),
                "CREATE TRIGGER integration_view_event_failure BEFORE INSERT "
                "ON exhibition_view_events FOR EACH ROW "
                "SET NEW.bootstrap_request_id = NULL");
        std::shared_ptr<std::promise<CountResult> > eventFailurePromise(
            new std::promise<CountResult>());
        std::shared_ptr<std::atomic<int> > eventFailureCallbacks(new std::atomic<int>(0));
        std::future<CountResult> eventFailureFuture = increment(
            &dao, exhibitionId, "non-duplicate-event-failure", eventFailurePromise,
            eventFailureCallbacks);
        const CountResult eventFailure = waitFor(&eventFailureFuture);
        execute(setup.get(), "DROP TRIGGER IF EXISTS integration_view_event_failure");
        CHECK(!eventFailure.ok && eventFailure.count == 0);
        CHECK(eventFailureCallbacks->load() == 1);
        CHECK(scalar(setup.get(),
                     "SELECT COUNT(*) FROM exhibition_view_events WHERE exhibition_id = '" +
                         exhibitionId + "'") == 0);
        CHECK(scalar(setup.get(),
                     "SELECT COUNT(*) FROM exhibition_statistics WHERE exhibition_id = '" +
                         exhibitionId + "'") == 0);

        execute(setup.get(), "DROP TRIGGER IF EXISTS integration_statistics_failure");
        execute(setup.get(),
                "CREATE TRIGGER integration_statistics_failure BEFORE INSERT "
                "ON exhibition_statistics FOR EACH ROW "
                "SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'expected integration failure'");
        std::shared_ptr<std::promise<CountResult> > failedPromise(
            new std::promise<CountResult>());
        std::shared_ptr<std::atomic<int> > failedCallbacks(new std::atomic<int>(0));
        std::future<CountResult> failedFuture = increment(
            &dao, exhibitionId, "failure-then-retry", failedPromise, failedCallbacks);
        const CountResult failed = waitFor(&failedFuture);
        execute(setup.get(), "DROP TRIGGER IF EXISTS integration_statistics_failure");
        CHECK(!failed.ok && failed.count == 0);
        CHECK(failedCallbacks->load() == 1);
        CHECK(scalar(setup.get(),
                     "SELECT COUNT(*) FROM exhibition_view_events WHERE exhibition_id = '" +
                         exhibitionId + "' AND bootstrap_request_id = 'failure-then-retry'") == 0);

        std::shared_ptr<std::promise<CountResult> > retryPromise(
            new std::promise<CountResult>());
        std::shared_ptr<std::atomic<int> > retryCallbacks(new std::atomic<int>(0));
        std::future<CountResult> retryFuture = increment(
            &dao, exhibitionId, "failure-then-retry", retryPromise, retryCallbacks);
        const CountResult retried = waitFor(&retryFuture);
        CHECK(retried.ok && retried.count == 1);

        std::vector<std::future<CountResult> > futures;
        std::vector<std::shared_ptr<std::atomic<int> > > callbacks;
        for (size_t index = 0; index < 8; ++index)
        {
            std::shared_ptr<std::promise<CountResult> > promise(
                new std::promise<CountResult>());
            std::shared_ptr<std::atomic<int> > callbackCount(new std::atomic<int>(0));
            callbacks.push_back(callbackCount);
            futures.push_back(increment(&dao, exhibitionId, "same-request", promise,
                                        callbackCount));
        }
        for (size_t index = 0; index < futures.size(); ++index)
        {
            const CountResult result = waitFor(&futures[index]);
            CHECK(result.ok);
            CHECK(result.count == 2);
            CHECK(callbacks[index]->load() == 1);
        }

        std::shared_ptr<std::promise<CountResult> > nextPromise(new std::promise<CountResult>());
        std::shared_ptr<std::atomic<int> > nextCallbacks(new std::atomic<int>(0));
        std::future<CountResult> nextFuture = increment(
            &dao, exhibitionId, "new-request", nextPromise, nextCallbacks);
        const CountResult next = waitFor(&nextFuture);
        CHECK(next.ok && next.count == 3);
        CHECK(nextCallbacks->load() == 1);
    }

    execute(setup.get(), "DELETE FROM exhibition_view_events WHERE exhibition_id = '" +
                         exhibitionId + "'");
    execute(setup.get(), "DELETE FROM exhibition_statistics WHERE exhibition_id = '" +
                         exhibitionId + "'");
    std::cout << "PASS: MySQL exhibition statistics idempotency" << std::endl;
    return 0;
}
