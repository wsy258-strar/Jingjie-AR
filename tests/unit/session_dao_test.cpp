#include "TestSupport.h"

#include <db/SessionDAO.h>

#include <memory>
#include <string>
#include <vector>

namespace {

void testUnavailableWorkerPoolCompletesEveryDaoCallbackWithDefault()
{
    SessionDAO dao(0);

    int callbackCount = 0;
    dao.findUserById(1, [&callbackCount](const std::shared_ptr<User>& user) {
        CHECK(!user);
        ++callbackCount;
    });
    dao.findUserByUsername("name", [&callbackCount](const std::shared_ptr<User>& user) {
        CHECK(!user);
        ++callbackCount;
    });
    dao.createUser("name", "hash", [&callbackCount](uint64_t id) {
        CHECK(id == 0);
        ++callbackCount;
    });
    dao.updatePasswordHash(1, "hash", [&callbackCount](bool ok) {
        CHECK(!ok);
        ++callbackCount;
    });
    dao.findSessionByToken("token", [&callbackCount](const std::shared_ptr<Session>& session) {
        CHECK(!session);
        ++callbackCount;
    });
    dao.createSession(1, "token", "scene", [&callbackCount](uint64_t id) {
        CHECK(id == 0);
        ++callbackCount;
    });
    dao.updateSessionScene(1, "scene", [&callbackCount](bool ok) {
        CHECK(!ok);
        ++callbackCount;
    });
    dao.endSession(1, [&callbackCount](bool ok) {
        CHECK(!ok);
        ++callbackCount;
    });
    dao.findActiveSessionsByUser(1, [&callbackCount](std::vector<Session> sessions) {
        CHECK(sessions.empty());
        ++callbackCount;
    });

    CHECK(callbackCount == 9);
}

} // namespace

int main()
{
    testUnavailableWorkerPoolCompletesEveryDaoCallbackWithDefault();
    return 0;
}
