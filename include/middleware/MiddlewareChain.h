// 有序中间件链：记录实际执行过的 before 阶段，以便按逆序配对执行 after 阶段。
#pragma once

#include <middleware/Middleware.h>

#include <memory>
#include <vector>

class MiddlewareChain
{
public:
    /// executed 由调用方保存，确保短路时不会为未执行项调用 after。
    void add(const std::shared_ptr<Middleware>& middleware);

    bool processBefore(HttpRequest& request, HttpResponse& response,
                       std::vector<std::shared_ptr<Middleware> >& executed) const;
    void processAfter(const HttpRequest& request, HttpResponse& response,
                      const std::vector<std::shared_ptr<Middleware> >& executed) const;

private:
    std::vector<std::shared_ptr<Middleware> > middleware_;
};
