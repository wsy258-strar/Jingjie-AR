#include <session/RedisSessionStorage.h>

#include <cstdint>
#include <limits>
#include <unordered_set>

namespace http { namespace session { namespace {
void put32(std::string* out, uint32_t value) { for (int s = 24; s >= 0; s -= 8) out->push_back(static_cast<char>((value >> s) & 0xff)); }
void put64(std::string* out, uint64_t value) { for (int s = 56; s >= 0; s -= 8) out->push_back(static_cast<char>((value >> s) & 0xff)); }
bool get32(const std::string& in, size_t* p, uint32_t* value) {
    if (*p > in.size() || in.size() - *p < 4) return false;
    *value = 0; for (int i = 0; i < 4; ++i) *value = (*value << 8) | static_cast<unsigned char>(in[*p + i]); *p += 4; return true;
}
bool get64(const std::string& in, size_t* p, uint64_t* value) {
    if (*p > in.size() || in.size() - *p < 8) return false;
    *value = 0; for (int i = 0; i < 8; ++i) *value = (*value << 8) | static_cast<unsigned char>(in[*p + i]); *p += 8; return true;
}
bool getString(const std::string& in, size_t* p, std::string* value) {
    uint32_t size = 0; if (!get32(in, p, &size) || size > in.size() - *p) return false;
    value->assign(in.data() + *p, size); *p += size; return true;
}
} // namespace

std::string RedisSessionStorage::key(const std::string& id) { return "http_session:{" + id + "}"; }
std::string RedisSessionStorage::encode(const Session& session) {
    const std::unordered_map<std::string, std::string>& values = session.values();
    if (session.id().size() > UINT32_MAX || values.size() > UINT32_MAX) return std::string();
    std::string out; put32(&out, static_cast<uint32_t>(session.id().size())); out.append(session.id());
    put64(&out, static_cast<uint64_t>(session.expiresAt())); put64(&out, static_cast<uint64_t>(session.ttlSeconds())); put32(&out, static_cast<uint32_t>(values.size()));
    for (std::unordered_map<std::string, std::string>::const_iterator it = values.begin(); it != values.end(); ++it) {
        if (it->first.size() > UINT32_MAX || it->second.size() > UINT32_MAX) return std::string();
        put32(&out, static_cast<uint32_t>(it->first.size())); out.append(it->first); put32(&out, static_cast<uint32_t>(it->second.size())); out.append(it->second);
    }
    return out;
}
bool RedisSessionStorage::decode(const std::string& in, Session* session) {
    if (!session) return false; size_t p = 0; std::string id; uint64_t expires = 0, ttl = 0; uint32_t count = 0;
    if (!getString(in, &p, &id) || !get64(in, &p, &expires) || !get64(in, &p, &ttl) || !get32(in, &p, &count)) return false;
    Session decoded(id, static_cast<int64_t>(expires), static_cast<int64_t>(ttl)); std::unordered_set<std::string> keys;
    for (uint32_t i = 0; i < count; ++i) { std::string key, value; if (!getString(in, &p, &key) || !getString(in, &p, &value) || !keys.insert(key).second) return false; decoded.setValue(key, value); }
    if (p != in.size()) return false; *session = decoded; return true;
}
} } // namespace http::session
