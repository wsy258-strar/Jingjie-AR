// 缓存优先会话存储实现：缓存层错误不被误报为“会话不存在”。
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

bool SessionCacheAdapter::putIfAbsent(const Session& session)
{
#ifdef HAS_REDIS
    return cache_ && cache_->putIfAbsent(session);
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
        durable_->find(token, [this, token, callback](const std::shared_ptr<Session>& session) {
            if (!session) { callback(session); return; }
            const Session copy = *session;
            if (!workers_->submit([this, token, copy, callback]() {
                if (copy.status == 0)
                {
                    callback(cache_->put(copy)
                        ? std::shared_ptr<Session>(new Session(copy))
                        : std::shared_ptr<Session>());
                    return;
                }
                if (cache_->putIfAbsent(copy))
                {
                    callback(std::shared_ptr<Session>(new Session(copy)));
                    return;
                }
                Session winner;
                callback(cache_->get(token, winner)
                    ? std::shared_ptr<Session>(new Session(winner))
                    : std::shared_ptr<Session>());
            })) callback(std::shared_ptr<Session>());
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

void CachedSessionStore::revoke(const std::string& token, const BoolCallback& callback)
{
    if (!durable_ || token.empty()) { callback(false); return; }
    durable_->revoke(token, [this, token, callback](bool revoked) {
        if (!revoked) { callback(false); return; }
        if (!cache_ || !workers_) { callback(true); return; }
        if (!workers_->submit([this, token, callback]() {
            const Session tombstone(0, token, 0, std::string(), 0,
                                    "revoked", "revoked");
            callback(cache_->put(tombstone));
        })) callback(false);
    });
}

} // namespace ar
