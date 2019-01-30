/* endpoint.cc
   Mathieu Stefani, 22 janvier 2016

   Implementation of the http endpoint
*/


#include <pistache/endpoint.h>
#include <pistache/tcp.h>
#include <pistache/peer.h>

namespace Pistache {
namespace Http {

Endpoint::Options::Options()
    : threads_(1)
    , flags_()
    , backlog_(Const::MaxBacklog)
    , maxPayload_(Const::DefaultMaxPayload)
{
  std::cout << __PRETTY_FUNCTION__ << std::endl;
}

Endpoint::Options&
Endpoint::Options::threads(int val) {
std::cout << __PRETTY_FUNCTION__ << std::endl;
    threads_ = val;
    return *this;
}

Endpoint::Options&
Endpoint::Options::flags(Flags<Tcp::Options> flags) {
std::cout << __PRETTY_FUNCTION__ << std::endl;
    flags_ = flags;
    return *this;
}

Endpoint::Options&
Endpoint::Options::backlog(int val) {
std::cout << __PRETTY_FUNCTION__ << std::endl;
    backlog_ = val;
    return *this;
}

Endpoint::Options&
Endpoint::Options::maxPayload(size_t val) {
std::cout << __PRETTY_FUNCTION__ << std::endl;
    maxPayload_ = val;
    return *this;
}

Endpoint::Endpoint()
{ }

Endpoint::Endpoint(const Address& addr)
    : listener(addr)
{ }

void
Endpoint::init(const Endpoint::Options& options) {
std::cout << __PRETTY_FUNCTION__ << std::endl;
    listener.init(options.threads_, options.flags_);
    ArrayStreamBuf<char>::maxSize = options.maxPayload_;
}

void
Endpoint::setHandler(const std::shared_ptr<Handler>& handler) {
std::cout << __PRETTY_FUNCTION__ << std::endl;
    handler_ = handler;
}

void
Endpoint::bind() {
std::cout << __PRETTY_FUNCTION__ << std::endl;
    listener.bind();
}

void
Endpoint::bind(const Address& addr) {
std::cout << __PRETTY_FUNCTION__ << std::endl;
    listener.bind(addr);
}

void
Endpoint::serve()
{
std::cout << __PRETTY_FUNCTION__ << std::endl;
  serveImpl(&Tcp::Listener::run);
}

void
Endpoint::serveThreaded()
{
std::cout << __PRETTY_FUNCTION__ << std::endl;
    serveImpl(&Tcp::Listener::runThreaded);
}

void
Endpoint::shutdown()
{
std::cout << __PRETTY_FUNCTION__ << std::endl;
    listener.shutdown();
}

void
Endpoint::useSSL(std::string cert, std::string key, bool use_compression)
{
#ifndef PISTACHE_USE_SSL
    (void)cert;
    (void)key;
    (void)use_compression;
    throw std::runtime_error("Pistache is not compiled with SSL support.");
#else
    listener.setupSSL(cert, key, use_compression);
#endif /* PISTACHE_USE_SSL */
}

void
Endpoint::useSSLAuth(std::string ca_file, std::string ca_path, int (*cb)(int, void *))
{
#ifndef PISTACHE_USE_SSL
    (void)ca_file;
    (void)ca_path;
    (void)cb;
    throw std::runtime_error("Pistache is not compiled with SSL support.");
#else
    listener.setupSSLAuth(ca_file, ca_path, cb);
#endif /* PISTACHE_USE_SSL */

}

Async::Promise<Tcp::Listener::Load>
Endpoint::requestLoad(const Tcp::Listener::Load& old) {
std::cout << __PRETTY_FUNCTION__ << std::endl;
    return listener.requestLoad(old);
}

Endpoint::Options
Endpoint::options() {
std::cout << __PRETTY_FUNCTION__ << std::endl;
    return Options();
}

} // namespace Http
} // namespace Pistache
