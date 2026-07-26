#include <services/CachedSessionStore.h>

#include <base/TaskWorkerPool.h>
#include <cache/SessionCache.h>

namespace ar {

bool SessionCacheAdapter::get(const std::string& token, Session& output)
{
#ifdef HAS_REDIS
    return cache_ && cache_->get(token, output);
#else
    (void)token; (void)output;
    return false;
#endif
}

bool SessionCacheAdapter::put(const Session& session)
{
#ifdef HAS_REDIS
    return cache_ && cache_->put(session);
#else
    (void)session;
    return false;
#endif
}

bool SessionCacheAdapter::remove(const std::string& token)
{
#ifdef HAS_REDIS
    return cache_ && cache_->remove(token);
#else
    (void)token;
    return false;
#endif
}

void CachedSessionStore::find(const std::string& token, const SessionCallback& callback)
{
    if (!durable_ || token.empty()) { callback(std::shared_ptr<Session>()); return; }
    if (!cache_ || !workers_)
    {
        durable_->find(token, callback);
        return;
    }
    if (!workers_->submit([this, token, callback]() {
        Session cached;
        if (cache_->get(token, cached))
        {
            callback(std::shared_ptr<Session>(new Session(cached)));
            return;
        }
        durable_->find(token, [this, callback](const std::shared_ptr<Session>& session) {
            callback(session);
            if (session && workers_)
            {
                const Session copy = *session;
                workers_->submit([this, copy]() { cache_->put(copy); });
            }
        });
    }))
    {
        callback(std::shared_ptr<Session>());
    }
}

void CachedSessionStore::enter(uint64_t sessionId, const std::string& sceneId, const BoolCallback& callback)
{
    if (!durable_) { callback(false); return; }
    durable_->enter(sessionId, sceneId, callback);
}

void CachedSessionStore::invalidate(const std::string& token)
{
    if (cache_ && workers_ && !token.empty())
        workers_->submit([this, token]() { cache_->remove(token); });
}

void CachedSessionStore::exit(uint64_t sessionId, const BoolCallback& callback)
{
    if (!durable_) { callback(false); return; }
    durable_->exit(sessionId, callback);
}

} // namespace ar
