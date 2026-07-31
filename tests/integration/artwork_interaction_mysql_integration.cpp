#include "TestSupport.h"

#include <db/ArtworkInteractionDAO.h>
#include <db/DBWorkerPool.h>
#include <db/MySQLConnectionPool.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iterator>
#include <mysql/mysql.h>
#include <string>

namespace {

struct LikeResult
{
    bool ok;
    bool changed;
    uint64_t count;
};

struct SummaryResult
{
    bool ok;
    uint64_t count;
    bool liked;
};

struct CommentResult
{
    bool ok;
    uint64_t id;
};

struct CommentsResult
{
    bool ok;
    std::vector<ArtworkComment> comments;
    uint64_t nextBefore;
};

template<typename Result>
Result waitFor(std::future<Result>* future)
{
    CHECK(future->wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    return future->get();
}

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

LikeResult like(ArtworkInteractionDAO* dao, const std::string& artworkId, uint64_t userId)
{
    std::promise<LikeResult> promise;
    std::future<LikeResult> future = promise.get_future();
    std::atomic<int> callbacks(0);
    dao->like(artworkId, userId,
              [&promise, &callbacks](bool ok, bool changed, uint64_t count) {
        CHECK(callbacks.fetch_add(1) == 0);
        LikeResult result = {ok, changed, count};
        promise.set_value(result);
    });
    LikeResult result = waitFor(&future);
    CHECK(callbacks.load() == 1);
    return result;
}

LikeResult unlike(ArtworkInteractionDAO* dao, const std::string& artworkId, uint64_t userId)
{
    std::promise<LikeResult> promise;
    std::future<LikeResult> future = promise.get_future();
    std::atomic<int> callbacks(0);
    dao->unlike(artworkId, userId,
                [&promise, &callbacks](bool ok, bool changed, uint64_t count) {
        CHECK(callbacks.fetch_add(1) == 0);
        LikeResult result = {ok, changed, count};
        promise.set_value(result);
    });
    LikeResult result = waitFor(&future);
    CHECK(callbacks.load() == 1);
    return result;
}

SummaryResult summary(ArtworkInteractionDAO* dao, const std::string& artworkId, uint64_t userId)
{
    std::promise<SummaryResult> promise;
    std::future<SummaryResult> future = promise.get_future();
    dao->summary(artworkId, userId,
                 [&promise](bool ok, uint64_t count, bool liked) {
        SummaryResult result = {ok, count, liked};
        promise.set_value(result);
    });
    return waitFor(&future);
}

CommentResult comment(ArtworkInteractionDAO* dao, const std::string& artworkId, uint64_t userId,
                      const std::string& content)
{
    std::promise<CommentResult> promise;
    std::future<CommentResult> future = promise.get_future();
    dao->createComment(artworkId, userId, content,
                       [&promise](bool ok, uint64_t id) {
        CommentResult result = {ok, id};
        promise.set_value(result);
    });
    return waitFor(&future);
}

CommentsResult comments(ArtworkInteractionDAO* dao, const std::string& artworkId,
                        uint64_t beforeId, uint32_t limit)
{
    std::promise<CommentsResult> promise;
    std::future<CommentsResult> future = promise.get_future();
    dao->listComments(
        artworkId, beforeId, limit,
        [&promise](bool ok, const std::vector<ArtworkComment>& values, uint64_t nextBefore) {
            CommentsResult result;
            result.ok = ok;
            result.comments = values;
            result.nextBefore = nextBefore;
            promise.set_value(result);
        });
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

    const std::string artworkId("integration-artwork-task4");
    const std::string username("integration-artwork-user-task4");
    execute(setup.get(), "DELETE FROM artwork_comments WHERE artwork_id = '" + artworkId + "'");
    execute(setup.get(), "DELETE FROM artwork_likes WHERE artwork_id = '" + artworkId + "'");
    execute(setup.get(), "DELETE FROM users WHERE username = '" + username + "'");
    execute(setup.get(),
            "INSERT INTO users (username, passwd_hash) VALUES ('" + username + "', 'integration')");
    const uint64_t userId = mysql_insert_id(setup.get());
    CHECK(userId != 0);

    {
        DBWorkerPool workers(&connections, 1);
        ArtworkInteractionDAO dao(&workers);

        const LikeResult firstLike = like(&dao, artworkId, userId);
        CHECK(firstLike.ok && firstLike.changed && firstLike.count == 1);
        const LikeResult duplicateLike = like(&dao, artworkId, userId);
        CHECK(duplicateLike.ok && !duplicateLike.changed && duplicateLike.count == 1);

        const SummaryResult authenticated = summary(&dao, artworkId, userId);
        CHECK(authenticated.ok && authenticated.count == 1 && authenticated.liked);
        const SummaryResult anonymous = summary(&dao, artworkId, 0);
        CHECK(anonymous.ok && anonymous.count == 1 && !anonymous.liked);

        const CommentResult first = comment(&dao, artworkId, userId, "第一条勤廉评论");
        const CommentResult second = comment(&dao, artworkId, userId, "第二条勤廉评论");
        const CommentResult third = comment(&dao, artworkId, userId, "第三条勤廉评论");
        CHECK(first.ok && second.ok && third.ok);
        CHECK(first.id < second.id && second.id < third.id);

        const CommentsResult firstPage = comments(&dao, artworkId, 0, 2);
        CHECK(firstPage.ok && firstPage.comments.size() == 2);
        CHECK(firstPage.comments[0].id == third.id);
        CHECK(firstPage.comments[1].id == second.id);
        CHECK(firstPage.comments[0].content == "第三条勤廉评论");
        CHECK(firstPage.nextBefore == second.id);

        const CommentsResult secondPage = comments(&dao, artworkId, firstPage.nextBefore, 2);
        CHECK(secondPage.ok && secondPage.comments.size() == 1);
        CHECK(secondPage.comments[0].id == first.id);
        CHECK(secondPage.comments[0].username == username);

        const LikeResult removed = unlike(&dao, artworkId, userId);
        CHECK(removed.ok && removed.changed && removed.count == 0);
        const SummaryResult afterUnlike = summary(&dao, artworkId, userId);
        CHECK(afterUnlike.ok && afterUnlike.count == 0 && !afterUnlike.liked);
    }

    execute(setup.get(), "DELETE FROM users WHERE id = " + std::to_string(userId));
    std::cout << "PASS: MySQL artwork interaction DAO" << std::endl;
    return 0;
}
