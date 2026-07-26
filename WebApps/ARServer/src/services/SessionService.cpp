#include <services/SessionService.h>

namespace ar {

void SessionService::get(const std::string& token, const SessionStore::SessionCallback& completion)
{
    if (store_ && !token.empty()) store_->find(token, completion);
    else completion(std::shared_ptr<Session>());
}

void SessionService::enter(const std::string& token, const std::string& sceneId,
                           const SessionStore::BoolCallback& completion)
{
    get(token, [this, token, sceneId, completion](const std::shared_ptr<Session>& session) {
        if (!session || !store_ || sceneId.empty()) { completion(false); return; }
        store_->enter(session->id, sceneId, [this, token, completion](bool ok) {
            if (ok) store_->invalidate(token);
            completion(ok);
        });
    });
}

void SessionService::exit(const std::string& token, const SessionStore::BoolCallback& completion)
{
    get(token, [this, token, completion](const std::shared_ptr<Session>& session) {
        if (!session || !store_ || session->status == 0) { completion(false); return; }
        store_->exit(session->id, [this, token, completion](bool ok) {
            if (ok) store_->invalidate(token);
            completion(ok);
        });
    });
}

} // namespace ar
