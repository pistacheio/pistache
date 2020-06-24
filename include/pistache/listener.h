/* listener.h
   Mathieu Stefani, 12 August 2015

  A TCP Listener
*/

#pragma once

#include <pistache/async.h>
#include <pistache/config.h>
#include <pistache/flags.h>
#include <pistache/log.h>
#include <pistache/net.h>
#include <pistache/os.h>
#include <pistache/reactor.h>
#include <pistache/ssl_wrappers.h>
#include <pistache/tcp.h>

#include <sys/resource.h>

#include <memory>
#include <thread>
#include <vector>

#ifdef PISTACHE_USE_SSL
#include <openssl/ssl.h>
#endif /* PISTACHE_USE_SSL */

namespace Pistache {
namespace Tcp {

class Peer;
class Transport;

void setSocketOptions(Fd fd, Flags<Options> options);

class Listener {
public:
  struct Load {
    using TimePoint = std::chrono::system_clock::time_point;
    double global;
    std::vector<double> workers;

    std::vector<rusage> raw;
    TimePoint tick;
  };

  Listener() = default;
  ~Listener();

  explicit Listener(const Address &address);
  void init(size_t workers,
            Flags<Options> options = Flags<Options>(Options::None),
            const std::string &workersName = "",
            int backlog = Const::MaxBacklog,
            PISTACHE_STRING_LOGGER_T logger = PISTACHE_NULL_STRING_LOGGER);
  void setHandler(const std::shared_ptr<Handler> &handler);

  void bind();
  void bind(const Address &address);

  bool isBound() const;
  Port getPort() const;

  void run();
  void runThreaded();

  void shutdown();

  Async::Promise<Load> requestLoad(const Load &old);

  Options options() const;
  Address address() const;

  void pinWorker(size_t worker, const CpuSet &set);

  void setupSSL(const std::string &cert_path, const std::string &key_path,
                bool use_compression);
  void setupSSLAuth(const std::string &ca_file, const std::string &ca_path,
                    int (*cb)(int, void *));

private:
  Address addr_;
  int listen_fd = -1;
  int backlog_ = Const::MaxBacklog;
  NotifyFd shutdownFd;
  Polling::Epoll poller;

  Flags<Options> options_;
  std::thread acceptThread;

  size_t workers_ = Const::DefaultWorkers;
  std::string workersName_;
  std::shared_ptr<Handler> handler_;

  Aio::Reactor reactor_;
  Aio::Reactor::Key transportKey;

  void handleNewConnection();
  int acceptConnection(struct sockaddr_in &peer_addr) const;
  void dispatchPeer(const std::shared_ptr<Peer> &peer);

  bool useSSL_ = false;
  ssl::SSLCtxPtr ssl_ctx_ = nullptr;

  PISTACHE_STRING_LOGGER_T logger_ = PISTACHE_NULL_STRING_LOGGER;
};

} // namespace Tcp
} // namespace Pistache
