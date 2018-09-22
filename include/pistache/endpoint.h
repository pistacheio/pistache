/*
   Mathieu Stefani, 22 janvier 2016

   An Http endpoint
*/

#pragma once

#include <pistache/listener.h>
#include <pistache/net.h>
#include <pistache/http.h>

namespace Pistache {
namespace Http {

class Endpoint {
public:
    struct Options {
        friend class Endpoint;

        Options& threads(int val);
        Options& flags(Flags<Tcp::Options> flags);
        Options& backlog(int val);
        Options& maxPayload(size_t val);

    private:
        int threads_;
        Flags<Tcp::Options> flags_;
        int backlog_;
        size_t maxPayload_;
        Options();
    };
    Endpoint();
    Endpoint(const Address& addr);

    template<typename... Args>
    void initArgs(Args&& ...args) {
        listener.init(std::forward<Args>(args)...);
    }

    void init(const Options& options);
    void setHandler(const std::shared_ptr<Handler>& handler);

    void bind();
    void bind(const Address& addr);

    void serve();
    void serveThreaded();

    void shutdown();

    bool isBound() const {
        return listener.isBound();
    }

    Async::Promise<Tcp::Listener::Load> requestLoad(const Tcp::Listener::Load& old);

    static Options options();

private:

    template<typename Method>
    void serveImpl(Method method)
    {
#define CALL_MEMBER_FN(obj, pmf)  ((obj).*(pmf))
        if (!handler_)
            throw std::runtime_error("Must call setHandler() prior to serve()");

        listener.setHandler(handler_);

        if (listener.bind()) {
            CALL_MEMBER_FN(listener, method)();
        }
#undef CALL_MEMBER_FN
    }

    std::shared_ptr<Handler> handler_;
    Tcp::Listener listener;
};

template<typename Handler>
void listenAndServe(Address addr)
{
    auto options = Endpoint::options().threads(1);
    listenAndServe<Handler>(addr, options);
}

template<typename Handler>
void listenAndServe(Address addr, const Endpoint::Options& options)
{
    Endpoint endpoint(addr);
    endpoint.init(options);
    endpoint.setHandler(make_handler<Handler>());
    endpoint.serve();
}


} // namespace Http
} // namespace Pistache
