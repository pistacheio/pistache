/* 
   Mathieu Stefani, 22 janvier 2016
   
   An Http endpoint
*/

#pragma once

#include "listener.h"
#include "net.h"
#include "http.h"
#include <functional>
#include <iostream>

namespace Net {

namespace Http {

class Endpoint {
public:
    struct Options {
        friend class Endpoint;

        Options& threads(int val);
        Options& flags(Flags<Tcp::Options> flags);
        Options& backlog(int val);

    private:
        int threads_;
        Flags<Tcp::Options> flags_;
        int backlog_;
        Options();
    };
    Endpoint();
    Endpoint(const Net::Address& addr);

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

    void setLogCallback(std::function<void(std::string)> cb){
        logCallback_ = cb;
    }

    Async::Promise<Tcp::Listener::Load> requestLoad(const Tcp::Listener::Load& old);

    static Options options();

private:

    std::function<void(std::string)> logCallback_;

    template<typename Method>
    void serveImpl(Method method)
    {
#define CALL_MEMBER_FN(obj, pmf)  ((obj).*(pmf))
        if (!handler_)
            throw std::runtime_error("Must call setHandler() prior to serve()");

        listener.setHandler(handler_);

        if (listener.bind()) {
            const auto& addr = listener.address();
            std::string out = "Now listening on http://";
            if(logCallback_)
            {
                out.append(addr.host());
                out.append(":");
                out.append(std::to_string(addr.port()));
                logCallback_(out);
            }
            CALL_MEMBER_FN(listener, method)();
        }
#undef CALL_MEMBER_FN
    }

    std::shared_ptr<Handler> handler_;
    Net::Tcp::Listener listener;
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

} // namespace Net
