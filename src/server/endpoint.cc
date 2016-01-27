/* endpoint.cc
   Mathieu Stefani, 22 janvier 2016
   
   Implementation of the http endpoint
*/


#include "endpoint.h"
#include "tcp.h"
#include "peer.h"

namespace Net {

namespace Http {


Endpoint::Options::Options()
    : threads_(1)
{ }

Endpoint::Options&
Endpoint::Options::threads(int val) {
    threads_ = val;
    return *this;
}

Endpoint::Options&
Endpoint::Options::flags(Flags<Tcp::Options> flags) {
    flags_ = flags;
    return *this;
}

Endpoint::Options&
Endpoint::Options::backlog(int val) {
    backlog_ = val;
    return *this;
}

Endpoint::Endpoint()
{ }

Endpoint::Endpoint(const Net::Address& addr)
    : listener(addr)
{ }

void
Endpoint::init(const Endpoint::Options& options) {
    listener.init(options.threads_, options.flags_);
}

void
Endpoint::setHandler(const std::shared_ptr<Handler>& handler) {
    handler_ = handler;
}

void
Endpoint::serve()
{
    serveImpl(&Tcp::Listener::run);
}

void
Endpoint::serveThreaded()
{
    serveImpl(&Tcp::Listener::runThreaded);
}

void
Endpoint::shutdown()
{
    listener.shutdown();
}

Async::Promise<Tcp::Listener::Load>
Endpoint::requestLoad(const Tcp::Listener::Load& old) {
    return listener.requestLoad(old);
}

Endpoint::Options
Endpoint::options() {
    return Options();
}

} // namespace Http

} // namespace Net
