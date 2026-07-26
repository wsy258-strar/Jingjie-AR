#include <middleware/MiddlewareChain.h>

#include <exception>

void MiddlewareChain::add(const std::shared_ptr<Middleware>& middleware)
{
    middleware_.push_back(middleware);
}

bool MiddlewareChain::processBefore(
    HttpRequest& request, HttpResponse& response,
    std::vector<std::shared_ptr<Middleware> >& executed) const
{
    for (std::vector<std::shared_ptr<Middleware> >::const_iterator it = middleware_.begin();
         it != middleware_.end(); ++it)
    {
        const bool continueRequest = (*it)->before(request, response);
        executed.push_back(*it);
        if (!continueRequest)
            return false;
    }
    return true;
}

void MiddlewareChain::processAfter(
    const HttpRequest& request, HttpResponse& response,
    const std::vector<std::shared_ptr<Middleware> >& executed) const
{
    std::exception_ptr failure;
    for (std::vector<std::shared_ptr<Middleware> >::const_reverse_iterator it = executed.rbegin();
         it != executed.rend(); ++it)
    {
        try
        {
            (*it)->after(request, response);
        }
        catch (...)
        {
            if (!failure)
            {
                failure = std::current_exception();
            }
        }
    }

    if (failure)
    {
        std::rethrow_exception(failure);
    }
}
