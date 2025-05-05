/*
 * SPDX-FileCopyrightText: 2015 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* listener.h
   Mathieu Stefani, 12 August 2015

  A TCP Listener
*/

#pragma once

#include <pistache/winornix.h>

#include <pistache/async.h>
#include <pistache/config.h>
#include <pistache/flags.h>
#include <pistache/log.h>
#include <pistache/net.h>
#include <pistache/os.h>
#include <pistache/reactor.h>
#include <pistache/ssl_wrappers.h>
#include <pistache/tcp.h>

#include PST_SYS_RESOURCE_HDR

#include <memory>
#include <thread>
#include <vector>

#ifdef PISTACHE_USE_SSL
#include <openssl/ssl.h>
#endif /* PISTACHE_USE_SSL */

namespace Pistache::Tcp
{

    class Peer;
    class Transport;

    void setSocketOptions(Fd fd, Flags<Options> options);

    class Listener
    {
    public:
        struct Load
        {
            using TimePoint = std::chrono::system_clock::time_point;
            double global;
            std::vector<double> workers;

            std::vector<PST_RUSAGE> raw;
            TimePoint tick;
        };

        using TransportFactory = std::function<std::shared_ptr<Transport>()>;

        Listener();
        ~Listener();

        explicit Listener(const Address& address);
        void init(size_t workers,
                  Flags<Options> options           = Flags<Options>(Options::None),
                  const std::string& workersName   = "",
                  int backlog                      = Const::MaxBacklog,
                  size_t acceptors                 = Const::DefaultAcceptors,
                  const std::string& acceptorsName = "",
                  PISTACHE_STRING_LOGGER_T logger  = PISTACHE_NULL_STRING_LOGGER);

        void setTransportFactory(TransportFactory factory);
        void setHandler(const std::shared_ptr<Handler>& handler);

        void bind();
        void bind(const Address& address);

        bool isBound() const;
        Port getPort() const;

        void run();
        void runThreaded();

        void shutdown();

        Async::Promise<Load> requestLoad(const Load& old);

        Options options() const;
        Address address() const;

        void pinWorker(size_t worker, const CpuSet& set);

        void setupSSL(const std::string& cert_path, const std::string& key_path,
                      bool use_compression, int (*cb_password)(char*, int, int, void*),
                      std::chrono::milliseconds sslHandshakeTimeout = Const::DefaultSSLHandshakeTimeout);
        void setupSSLAuth(const std::string& ca_file, const std::string& ca_path,
                          int (*cb)(int, void*));
        std::vector<std::shared_ptr<Tcp::Peer>> getAllPeer();

    private:
        Address addr_;
        Fd listen_fd = PS_FD_EMPTY;
        int backlog_ = Const::MaxBacklog;
        NotifyFd shutdownFd;
        Polling::Epoll poller;

        Flags<Options> options_;
        std::thread acceptThread;

        size_t acceptors_ = Const::DefaultAcceptors;
        std::string acceptorsName_;
        std::vector<std::thread> acceptWorkers;

        size_t workers_ = Const::DefaultWorkers;
        std::string workersName_;
        std::shared_ptr<Handler> handler_;

        std::shared_ptr<Aio::Reactor> reactor_;
        Aio::Reactor::Key transportKey;

        TransportFactory transportFactory_;

        TransportFactory defaultTransportFactory() const;

        bool bindListener(const struct addrinfo* addr);
        void acceptWorkerFn();

        void handleNewConnection();
        em_socket_t acceptConnection(struct sockaddr_storage& peer_addr) const;
        void dispatchPeer(const std::shared_ptr<Peer>& peer);

#ifdef _IS_WINDOWS
        std::atomic<em_socket_t> idxCtr_ = 1;
#endif
        bool useSSL_            = false;
        ssl::SSLCtxPtr ssl_ctx_ = nullptr;

        PISTACHE_STRING_LOGGER_T logger_ = PISTACHE_NULL_STRING_LOGGER;

        // This should be moved after "ssl_ctx_" in the next ABI change
        std::chrono::milliseconds sslHandshakeTimeout_ = Const::DefaultSSLHandshakeTimeout;
    };

} // namespace Pistache::Tcp
