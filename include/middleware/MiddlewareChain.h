#pragma once

#include <middleware/Middleware.h>

#include <memory>
#include <vector>

class MiddlewareChain
{
public:
    void add(const std::shared_ptr<Middleware>& middleware);

    bool processBefore(HttpRequest& request, HttpResponse& response,
                       std::vector<std::shared_ptr<Middleware> >& executed) const;
    void processAfter(const HttpRequest& request, HttpResponse& response,
                      const std::vector<std::shared_ptr<Middleware> >& executed) const;

private:
    std::vector<std::shared_ptr<Middleware> > middleware_;
};
