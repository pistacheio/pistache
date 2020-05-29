/* endpoint.cc
   Mathieu Stefani, 22 janvier 2016

   Implementation of the http endpoint
*/

#include <pistache/config.h>
#include <pistache/endpoint.h>
#include <pistache/peer.h>
#include <pistache/tcp.h>

namespace Pistache {
namespace Http {

Endpoint::Options::Options()
    : threads_(1), flags_(), backlog_(Const::MaxBacklog),
      maxRequestSize_(Const::DefaultMaxRequestSize),
      maxResponseSize_(Const::DefaultMaxResponseSize),
      logger_(PISTACHE_NULL_STRING_LOGGER) {}

Endpoint::Options &Endpoint::Options::threads(int val) {
  threads_ = val;
  return *this;
}

Endpoint::Options &Endpoint::Options::threadsName(const std::string &val) {
  threadsName_ = val;
  return *this;
}

Endpoint::Options &Endpoint::Options::flags(Flags<Tcp::Options> flags) {
  flags_ = flags;
  return *this;
}

Endpoint::Options &Endpoint::Options::backlog(int val) {
  backlog_ = val;
  return *this;
}

Endpoint::Options &Endpoint::Options::maxRequestSize(size_t val) {
  maxRequestSize_ = val;
  return *this;
}

Endpoint::Options &Endpoint::Options::maxPayload(size_t val) {
  return maxRequestSize(val);
}

Endpoint::Options &Endpoint::Options::maxResponseSize(size_t val) {
  maxResponseSize_ = val;
  return *this;
}

Endpoint::Options &Endpoint::Options::logger(PISTACHE_STRING_LOGGER_T logger) {
  logger_ = logger;
  return *this;
}

Endpoint::Endpoint() {}

Endpoint::Endpoint(const Address &addr) : listener(addr) {}

void Endpoint::init(const Endpoint::Options &options) {
  listener.init(options.threads_, options.flags_, options.threadsName_);
  maxRequestSize_ = options.maxRequestSize_;
  maxResponseSize_ = options.maxResponseSize_;
  logger_ = options.logger_;
}

void Endpoint::setHandler(const std::shared_ptr<Handler> &handler) {
  handler_ = handler;
  handler_->setMaxRequestSize(maxRequestSize_);
  handler_->setMaxResponseSize(maxResponseSize_);
}

void Endpoint::bind() { listener.bind(); }

void Endpoint::bind(const Address &addr) { listener.bind(addr); }

void Endpoint::serve() { serveImpl(&Tcp::Listener::run); }

void Endpoint::serveThreaded() { serveImpl(&Tcp::Listener::runThreaded); }

void Endpoint::shutdown() { listener.shutdown(); }

void Endpoint::useSSL(const std::string &cert, const std::string &key, bool use_compression) {
#ifndef PISTACHE_USE_SSL
  (void)cert;
  (void)key;
  (void)use_compression;
  throw std::runtime_error("Pistache is not compiled with SSL support.");
#else
  listener.setupSSL(cert, key, use_compression);
#endif /* PISTACHE_USE_SSL */
}

void Endpoint::useSSLAuth(std::string ca_file, std::string ca_path,
                          int (*cb)(int, void *)) {
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
Endpoint::requestLoad(const Tcp::Listener::Load &old) {
  return listener.requestLoad(old);
}

Endpoint::Options Endpoint::options() { return Options(); }

} // namespace Http
} // namespace Pistache
