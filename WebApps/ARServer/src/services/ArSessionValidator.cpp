// AR 会话令牌解析与验证实现。
#include <services/ArSessionValidator.h>

#include <services/SessionService.h>

namespace ar {

void ArSessionValidator::validate(const std::string& token,
                                  const std::function<void(bool)>& completion)
{
    if (!sessions_ || token.empty())
    {
        completion(false);
        return;
    }
    sessions_->get(token, [completion](const std::shared_ptr<Session>& session) {
        completion(session && session->status == 1);
    });
}

} // namespace ar
