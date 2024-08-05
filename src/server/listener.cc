/*
 * SPDX-FileCopyrightText: 2015 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* listener.cc
   Mathieu Stefani, 12 August 2015

*/

#include <pistache/common.h>
#include <pistache/errors.h>
#include <pistache/listener.h>
#include <pistache/os.h>
#include <pistache/peer.h>
#include <pistache/ssl_wrappers.h>
#include <pistache/transport.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <pistache/eventmeth.h>

#ifndef _USE_LIBEVENT_LIKE_APPLE
#include <unistd.h>
#endif
#include <fcntl.h>

#ifndef _USE_LIBEVENT
#include <sys/epoll.h>
#endif

#ifdef _USE_LIBEVENT_LIKE_APPLE
#include <netdb.h> // for getprotobyname
#endif

#include <sys/socket.h>

#ifndef _USE_LIBEVENT_LIKE_APPLE
// Note: sys/timerfd.h is linux-only (and certainly POSIX only)
#include <sys/timerfd.h>
#endif

#include <sys/types.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <cerrno>
#include <signal.h>

#ifdef PISTACHE_USE_SSL

#include <openssl/err.h>
#include <openssl/ssl.h>

#endif /* PISTACHE_USE_SSL */

using namespace std::chrono_literals;

namespace Pistache::Tcp
{

#ifdef PISTACHE_USE_SSL

    namespace
    {

        std::string ssl_print_errors_to_string()
        {
            ssl::SSLBioPtr bio { BIO_new(BIO_s_mem()) };
            ERR_print_errors(GetSSLBio(bio));

            static const int buffer_length = 512;

            bool continue_reading = true;
            char buffer[buffer_length];
            std::string result;

            while (continue_reading)
            {
                int ret = BIO_gets(GetSSLBio(bio), buffer, buffer_length);
                switch (ret)
                {
                case 0:
                case -1:
                    // Reached the end of the BIO, or it is unreadable for some reason.
                    continue_reading = false;
                    break;
                case -2:
                    throw std::logic_error("Trying to call PopStringFromBio on a BIO that "
                                           "does not support the BIO_gets method");
                    break;
                default: // >0
                    result.append(buffer);
                    break;
                }
            }

            return result;
        }

        ssl::SSLCtxPtr ssl_create_context(const std::string& cert,
                                          const std::string& key,
                                          bool use_compression,
                                          int (*cb)(char*, int, int, void*))
        {
            PS_TIMEDBG_START;

            const SSL_METHOD* method = SSLv23_server_method();

            ssl::SSLCtxPtr ctx { SSL_CTX_new(method) };
            if (ctx == nullptr)
            {
                PS_LOG_DEBUG("Cannot setup SSL context");
                throw std::runtime_error("Cannot setup SSL context");
            }

            if (!use_compression)
            {
                PS_LOG_DEBUG("Disable SSL compression");

                /* Disable compression to prevent BREACH and CRIME vulnerabilities. */
                if (!SSL_CTX_set_options(GetSSLContext(ctx), SSL_OP_NO_COMPRESSION))
                {
                    std::string err = "SSL error - cannot disable compression: "
                        + ssl_print_errors_to_string();

                    PS_LOG_DEBUG_ARGS("%s", err.c_str());

                    throw std::runtime_error(err);
                }
            }

            if (cb != NULL)
            {
                /* Use the user-defined callback for password if provided */
                SSL_CTX_set_default_passwd_cb(GetSSLContext(ctx), cb);
            }

/* Function introduced in 1.0.2 */
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
            SSL_CTX_set_ecdh_auto(GetSSLContext(ctx), 1);
#endif /* OPENSSL_VERSION_NUMBER */

            if (SSL_CTX_use_certificate_chain_file(GetSSLContext(ctx), cert.c_str()) <= 0)
            {
                std::string err = "SSL error - cannot load SSL certificate: "
                    + ssl_print_errors_to_string();
                PS_LOG_DEBUG_ARGS("%s", err.c_str());

                throw std::runtime_error(err);
            }

            if (SSL_CTX_use_PrivateKey_file(GetSSLContext(ctx), key.c_str(), SSL_FILETYPE_PEM) <= 0)
            {
                std::string err = "SSL error - cannot load SSL private key: "
                    + ssl_print_errors_to_string();
                PS_LOG_DEBUG_ARGS("%s", err.c_str());

                throw std::runtime_error(err);
            }

            if (!SSL_CTX_check_private_key(GetSSLContext(ctx)))
            {
                std::string err = "SSL error - Private key does not match certificate public key: "
                    + ssl_print_errors_to_string();
                PS_LOG_DEBUG_ARGS("%s", err.c_str());
                throw std::runtime_error(err);
            }

            SSL_CTX_set_mode(GetSSLContext(ctx), SSL_MODE_ENABLE_PARTIAL_WRITE);
            SSL_CTX_set_mode(GetSSLContext(ctx), SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
            return ctx;
        }

    }
#endif /* PISTACHE_USE_SSL */

    void setSocketOptions(int actualFd, Flags<Options> options)
    {
        PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
        if (options.hasFlag(Options::CloseOnExec))
        {
            int f_setfd_flags = fcntl(actualFd, F_GETFD, (int)0);
            if (!(f_setfd_flags & FD_CLOEXEC))
            {
                f_setfd_flags |= FD_CLOEXEC;
                int fcntl_res = fcntl(actualFd, F_SETFD, f_setfd_flags);
                if (fcntl_res == -1)
                    throw std::runtime_error("fcntl set failed");
            }
        }
#endif

        if (options.hasFlag(Options::ReuseAddr))
        {
            PS_LOG_DEBUG("Set SO_REUSEADDR");

            int one = 1;
            TRY(::setsockopt(actualFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)));
        }

        if (options.hasFlag(Options::ReusePort))
        {
            PS_LOG_DEBUG("Set SO_REUSEPORT");
            int one = 1;
            TRY(::setsockopt(actualFd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)));
        }

        if (options.hasFlag(Options::Linger))
        {
            struct linger opt;
            opt.l_onoff  = 1;
            opt.l_linger = 1;
            TRY(::setsockopt(actualFd, SOL_SOCKET, SO_LINGER, &opt, sizeof(opt)));
        }

#ifdef _USE_LIBEVENT_LIKE_APPLE
        // SOL_TCP not defined in macOS Nov 2023
        const struct protoent* pe = getprotobyname("tcp");
        int tcp_prot_num          = pe ? pe->p_proto : 6;
#ifdef DEBUG
#ifdef __linux__
        assert(tcp_prot_num == SOL_TCP);
#endif
#endif
#else
        int tcp_prot_num = SOL_TCP;
#endif

        if (options.hasFlag(Options::FastOpen))
        {
            int hint = 5;
            TRY(::setsockopt(actualFd, tcp_prot_num,
                             TCP_FASTOPEN, &hint, sizeof(hint)));
        }
        if (options.hasFlag(Options::NoDelay))
        {
            int one = 1;
            TRY(::setsockopt(actualFd, tcp_prot_num,
                             TCP_NODELAY, &one, sizeof(one)));
        }
    }

    Listener::Listener()
        : transportFactory_(defaultTransportFactory())
    { }

    Listener::Listener(const Address& address)
        : addr_(address)
        , transportFactory_(defaultTransportFactory())
    { }

    Listener::~Listener()
    {
        if (isBound())
            shutdown();
        if (acceptThread.joinable())
            acceptThread.join();

        if (listen_fd != PS_FD_EMPTY)
        {
            CLOSE_FD(listen_fd);
            listen_fd = PS_FD_EMPTY;
        }
    }

    void Listener::init(size_t workers, Flags<Options> options,
                        const std::string& workersName, int backlog,
                        PISTACHE_STRING_LOGGER_T logger)
    {
        if (workers > hardware_concurrency())
        {
            // Log::warning() << "More workers than available cores"
        }

        options_     = options;
        backlog_     = backlog;
        useSSL_      = false;
        workers_     = workers;
        workersName_ = workersName;
        logger_      = logger;
    }

    void Listener::setTransportFactory(TransportFactory factory)
    {
        transportFactory_ = std::move(factory);
    }

    void Listener::setHandler(const std::shared_ptr<Handler>& handler)
    {
        handler_ = handler;
    }

    void Listener::pinWorker([[maybe_unused]] size_t worker, [[maybe_unused]] const CpuSet& set)
    {
#if 0
    if (ioGroup.empty()) {
        throw std::domain_error("Invalid operation, did you call init() before ?");
    }
    if (worker > ioGroup.size()) {
        throw std::invalid_argument("Trying to pin invalid worker");
    }

    auto &wrk = ioGroup[worker];
    wrk->pin(set);
#endif
    }

    void Listener::bind() { bind(addr_); }

    // Abstracts out binding-related processing common to both IP-based sockets
    // and unix domain-based sockets.  Called from bind()  below.
    //
    // Attempts to bind the address described by addr and set up a
    // corresponding socket as a listener, returning true upon success and
    // false on failure.  Sets listen_fd on success.
    bool Listener::bindListener(const struct addrinfo* addr)
    {
        PS_TIMEDBG_START_THIS;

        auto socktype = addr->ai_socktype;
// SOCK_CLOEXEC not defined in macOS Nov 2023
// In the _USE_LIBEVENT_LIKE_APPLE case, we set FD_CLOEXEC using fcntl
// in the setSocketOptions function that is invoked below
#ifndef _USE_LIBEVENT_LIKE_APPLE
        if (options_.hasFlag(Options::CloseOnExec))
            socktype |= SOCK_CLOEXEC;
#endif

        int actual_fd = ::socket(addr->ai_family, socktype, addr->ai_protocol);
        PS_LOG_DEBUG_ARGS("::socket actual_fd %d", actual_fd);

        if (actual_fd < 0)
        {
            PS_LOG_DEBUG("::socket failed");
            return false;
        }

        LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(actual_fd);

        setSocketOptions(actual_fd, options_);

        LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(actual_fd);

        if (::bind(actual_fd, addr->ai_addr, addr->ai_addrlen) < 0)
        {
            PS_LOG_DEBUG_ARGS("::bind failed, actual_fd %d", actual_fd);
            close(actual_fd);
            return false;
        }

        LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(actual_fd);

        TRY(::listen(actual_fd, backlog_));

        LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(actual_fd);

#ifdef DEBUG
        bool mnb_res =
#endif
            make_non_blocking(actual_fd);
#ifdef DEBUG
        if (!mnb_res)
            PS_LOG_DEBUG_ARGS("make_non_blocking failed for fd %d",
                              actual_fd);
#endif

        LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(actual_fd);

#ifdef _USE_LIBEVENT
        // Use EVM_READ, as per call to addFd below
        Fd event_fd = TRY_NULL_RET(
            Polling::Epoll::em_event_new(actual_fd, // pre-allocated file desc
                                         EVM_READ | EVM_PERSIST,
                                         F_SETFDL_NOTHING, // f_setfd_flags - don't change
                                         F_SETFDL_NOTHING // f_setfl_flags - don't change
                                         ));
#else
        Fd event_fd = actual_fd;
#endif

        LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(actual_fd);

        PS_LOG_DEBUG_ARGS("Add read fd %" PIST_QUOTE(PS_FD_PRNTFCD), event_fd);
        poller.addFd(event_fd,
                     Flags<Polling::NotifyOn>(Polling::NotifyOn::Read),
                     Polling::Tag(event_fd));
        listen_fd = event_fd;

        LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(actual_fd);

        auto transport = transportFactory_();

        reactor_ = std::make_shared<Aio::Reactor>();
        reactor_->init(Aio::AsyncContext(workers_, workersName_));

        transportKey = reactor_->addHandler(transport);

        LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(actual_fd);

        return true;
    }

    void Listener::bind(const Address& address)
    {
        PS_TIMEDBG_START_THIS;

        addr_ = address;

        auto found            = false;
        const auto family     = address.family();
        struct addrinfo hints = {};
        hints.ai_family       = family;
        hints.ai_socktype     = SOCK_STREAM;
        hints.ai_flags        = AI_PASSIVE;

        if (family == AF_UNIX)
        {
            const struct sockaddr& sa = address.getSockAddr();
            // unix domain sockets are confined to the local host, so there's
            // no question of finding the best address.  It's simply the one
            // hiding inside the address object.
            //
            // Impedance match the unix domain address into a suitable argument
            // to bindListener().
            hints.ai_protocol = 0;
            hints.ai_addr     = const_cast<struct sockaddr*>(&sa);
            hints.ai_addrlen  = address.addrLen();
            found             = bindListener(&hints);
        }
        else
        {
            const auto& host = addr_.host();
            const auto& port = addr_.port().toString();
            AddrInfo addr_info;

            TRY(addr_info.invoke(host.c_str(), port.c_str(), &hints));

            const addrinfo* addr = nullptr;
            for (addr = addr_info.get_info_ptr(); addr; addr = addr->ai_next)
            {
                found = bindListener(addr);
                if (found)
                {
                    break;
                }
            }
        }

        //
        // At this point, it is still possible that we couldn't bind any socket.
        //
        if (!found)
        {
            throw std::runtime_error(strerror(errno));
        }
    }

    bool Listener::isBound() const { return listen_fd != PS_FD_EMPTY; }

    // Return actual TCP port Listener is on, or 0 on error / no port.
    // Notes:
    // 1) Default constructor for 'Port()' sets value to 0.
    // 2) Socket is created inside 'Listener::run()', which is called from
    //    'Endpoint::serve()' and 'Endpoint::serveThreaded()'.  So getting the
    //    port is only useful if you attempt to do so from a _different_ thread
    //    than the one running 'Listener::run()'.  So for a traditional single-
    //    threaded program this method is of little value.
    Port Listener::getPort() const
    {
        if (listen_fd == PS_FD_EMPTY)
        {
            return Port();
        }

        struct sockaddr_storage sock_addr = {};
        socklen_t addrlen                 = sizeof(sock_addr);
        auto* sock_addr_alias             = reinterpret_cast<struct sockaddr*>(&sock_addr);

        if (-1 == getsockname(GET_ACTUAL_FD(listen_fd), sock_addr_alias, &addrlen))
        {
            return Port();
        }

        if (sock_addr.ss_family == AF_INET)
        {
            auto* sock_addr_in = reinterpret_cast<struct sockaddr_in*>(&sock_addr);
            return Port(ntohs(sock_addr_in->sin_port));
        }
        else if (sock_addr.ss_family == AF_INET6)
        {
            auto* sock_addr_in6 = reinterpret_cast<struct sockaddr_in6*>(&sock_addr);
            return Port(ntohs(sock_addr_in6->sin6_port));
        }
        else
        {
            return Port();
        }
    }

    void Listener::run()
    {
        if (!shutdownFd.isBound())
            shutdownFd.bind(poller);
        reactor_->run();

        for (;;)
        {
            { // encapsulate l_guard(poller.reg_unreg_mutex_)
              // See comment in class Epoll regarding reg_unreg_mutex_

                std::lock_guard<std::mutex> l_guard(poller.reg_unreg_mutex_);

                std::vector<Polling::Event> events;
                int ready_fds = poller.poll(events);

                if (ready_fds == -1)
                {
                    throw Error::system("Polling");
                }
                for (const auto& event : events)
                {
                    if (event.tag == shutdownFd.tag())
                        return;

                    if (event.flags.hasFlag(Polling::NotifyOn::Read))
                    {
                        Fd fd = (Fd)event.tag.value();
                        if (fd == listen_fd)
                        {
                            try
                            {
                                handleNewConnection();
                            }
                            catch (SocketError& ex)
                            {
                                PISTACHE_LOG_STRING_WARN(
                                    logger_, "Socket error: " << ex.what());
                            }
                            catch (ServerError& ex)
                            {
                                PISTACHE_LOG_STRING_FATAL(
                                    logger_, "Server error: " << ex.what());
                                throw;
                            }
                        }
                    }
                }
            }
        }
    }

    void Listener::runThreaded()
    {
        shutdownFd.bind(poller);
        acceptThread = std::thread([=]() { this->run(); });
    }

    void Listener::shutdown()
    {
        if (shutdownFd.isBound())
        {
            PS_TIMEDBG_START_CURLY;

            shutdownFd.notify();
        }

        if (reactor_)
            reactor_->shutdown();
    }

    Async::Promise<Listener::Load>
    Listener::requestLoad(const Listener::Load& old)
    {
        PS_TIMEDBG_START_THIS;

        auto handlers = reactor_->handlers(transportKey);

        std::vector<Async::Promise<rusage>> loads;
        for (const auto& handler : handlers)
        {
            auto transport = std::static_pointer_cast<Transport>(handler);
            loads.push_back(transport->load());
        }

        return Async::whenAll(std::begin(loads), std::end(loads))
            .then(
                [=](const std::vector<rusage>& usages) {
                    PS_TIMEDBG_START;
                    Load res;
                    res.raw = usages;

                    if (old.raw.empty())
                    {
                        res.global = 0.0;
                        for (size_t i = 0; i < handlers.size(); ++i)
                            res.workers.push_back(0.0);
                    }
                    else
                    {

                        auto totalElapsed = [](rusage usage) {
                            return static_cast<double>((usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec) + (usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec));
                        };

                        auto now  = std::chrono::system_clock::now();
                        auto diff = now - old.tick;
                        auto tick = std::chrono::duration_cast<std::chrono::microseconds>(diff);
                        res.tick  = now;

                        for (size_t i = 0; i < usages.size(); ++i)
                        {
                            auto last         = old.raw[i];
                            const auto& usage = usages[i];

                            auto nowElapsed  = totalElapsed(usage);
                            auto timeElapsed = nowElapsed - totalElapsed(last);

                            auto loadPct = (timeElapsed * 100.0) / static_cast<double>(tick.count());
                            res.workers.push_back(loadPct);
                            res.global += loadPct;
                        }

                        res.global /= static_cast<double>(usages.size());
                    }

                    return res;
                },
                Async::Throw);
    }

    Address Listener::address() const { return addr_; }

    Options Listener::options() const { return options_; }

    void Listener::handleNewConnection()
    {
        PS_TIMEDBG_START_THIS;

        struct sockaddr_storage peer_addr;
        int actual_cli_fd = acceptConnection(peer_addr);

        void* ssl = nullptr;

#ifdef PISTACHE_USE_SSL
        if (this->useSSL_)
        {
            PS_LOG_DEBUG("SSL connection");

            SSL* ssl_data = SSL_new(GetSSLContext(ssl_ctx_));
            if (ssl_data == nullptr)
            {
                PS_LOG_DEBUG("SSL_new failed");

                close(actual_cli_fd);
                std::string err = "SSL error - cannot create SSL connection: "
                    + ssl_print_errors_to_string();
                throw ServerError(err.c_str());
            }

            // If user requested SSL handshake timeout, enable it on the
            //  socket.  This is sometimes necessary if a client connects,
            //  sends nothing, or possibly refuses to accept any bytes, and
            //  never completes a handshake. This would have left SSL_accept
            //  hanging indefinitely and is effectively a DoS...
            if (sslHandshakeTimeout_ > 0ms)
            {
                PS_LOG_DEBUG("SSL timeout to be set");

                struct timeval timeout;

                timeout.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(sslHandshakeTimeout_).count();

                const auto residual_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(sslHandshakeTimeout_) - std::chrono::duration_cast<std::chrono::seconds>(sslHandshakeTimeout_);
                timeout.tv_usec                  = (suseconds_t)(residual_microseconds.count());

                TRY(::setsockopt(actual_cli_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
                TRY(::setsockopt(actual_cli_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)));
            }

            SSL_set_fd(ssl_data, actual_cli_fd);
            SSL_set_accept_state(ssl_data);

            int ssl_accept_res = SSL_accept(ssl_data);

            if (ssl_accept_res <= 0)
            {
                PS_LOG_DEBUG_ARGS("SSL_accept failed, ssl_accept_res %d, "
                                  "actual_cli_fd %d",
                                  ssl_accept_res, actual_cli_fd);

#ifdef DEBUG
                const char* ssl_ver = OPENSSL_VERSION_TEXT;
                PS_LOG_DEBUG_ARGS("openssl: %s", ssl_ver);
#endif

#ifdef DEBUG
                int ssl_err_code = SSL_get_error(ssl_data, ssl_accept_res);
                PS_LOG_DEBUG_ARGS("ssl_err_code %d", ssl_err_code);
#endif

                std::string err = "SSL connection error: "
                    + ssl_print_errors_to_string();

                PS_LOG_DEBUG_ARGS("%s", err.c_str());

                PISTACHE_LOG_STRING_INFO(logger_, err);
                SSL_free(ssl_data);
                close(actual_cli_fd);
                return;
            }

            PS_LOG_DEBUG("SSL_accept succcess");

            // Remove socket timeouts if they were enabled now that we have
            //  handshaked...
            if (sslHandshakeTimeout_ > 0ms)
            {
                PS_LOG_DEBUG("SSL timeout to be removed");

                struct timeval timeout;
                timeout.tv_sec  = 0;
                timeout.tv_usec = 0;

                TRY(::setsockopt(actual_cli_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
                TRY(::setsockopt(actual_cli_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)));
            }

            ssl = static_cast<void*>(ssl_data);
        }
#endif /* PISTACHE_USE_SSL */

#ifdef DEBUG
        bool mnb_res =
#endif
            make_non_blocking(actual_cli_fd);
#ifdef DEBUG
        if (!mnb_res)
            PS_LOG_DEBUG_ARGS("make_non_blocking failed for fd %d",
                              actual_cli_fd);
#endif

#ifdef _USE_LIBEVENT
        // Since we're accepting a remote connection here, presumably it makes
        // sense to have it be able to read *or* write?
        Fd client_fd = TRY_NULL_RET(
            Polling::Epoll::em_event_new(actual_cli_fd, // pre-allocated file dsc
                                         EVM_READ | EVM_WRITE | EVM_PERSIST,
                                         F_SETFDL_NOTHING, // f_setfd_flags - don't change
                                         F_SETFDL_NOTHING // f_setfl_flags - don't change
                                         ));
#else
        Fd client_fd = actual_cli_fd;
#endif

        std::shared_ptr<Peer> peer;
        auto* peer_alias = reinterpret_cast<struct sockaddr*>(&peer_addr);
        if (this->useSSL_)
        {
            PS_LOG_DEBUG("Calling Peer::CreateSSL");

            peer = Peer::CreateSSL(client_fd, Address::fromUnix(peer_alias), ssl);
        }
        else
        {
            PS_LOG_DEBUG("Calling Peer::Create(");

            peer = Peer::Create(client_fd, Address::fromUnix(peer_alias));
        }

        PS_LOG_DEBUG_ARGS("Calling dispatchPeer %p", peer.get());
        dispatchPeer(peer);
    }

    int Listener::acceptConnection(struct sockaddr_storage& peer_addr) const
    {
        PS_TIMEDBG_START_THIS;

        socklen_t peer_addr_len = sizeof(peer_addr);

        int listen_fd_actual = GET_ACTUAL_FD(listen_fd);

        PS_LOG_DEBUG_ARGS("listen_fd %" PIST_QUOTE(PS_FD_PRNTFCD) ", "
                                                                  "listen_fd_actual %d",
                          listen_fd, listen_fd_actual);

        LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(listen_fd_actual);

        // Do not share open FD with forked processes
        int client_actual_fd =
#ifdef _USE_LIBEVENT_LIKE_APPLE
            ::accept(listen_fd_actual,
                     reinterpret_cast<struct sockaddr*>(&peer_addr),
                     &peer_addr_len);
// Note: macOS doesn't support accept4 nor SOCK_CLOEXEC as of Nov-2023
// accept4 is an extended form of "accept" with additional flags

// Linux man page for "accept"
//   On Linux, the new socket returned by accept() does not inherit
//   file status flags such as O_NONBLOCK and O_ASYNC from the
//   listening socket. This behavior differs from the canonical BSD
//   sockets implementation. Portable programs should not rely on
//   inheritance or noninheritance of file status flags and always
//   explicitly set all required flags on the socket returned from
//   accept().
//
// macOS man page for "accept"
//   ...creates a new socket with the same properties of
//   socket['socket' = the listen fd]...
//
// So the Linux "accept" has the additional side-effect of clearing all
// GETFD and GETFL flags; and "accept4" then sets CLOEXEC. We emulate
// the same behaviour below.
#else
            ::accept4(listen_fd_actual,
                      reinterpret_cast<struct sockaddr*>(&peer_addr),
                      &peer_addr_len, SOCK_CLOEXEC);
#endif
        PS_LOG_DEBUG_ARGS("::accept(4) ::socket actual_fd %d",
                          client_actual_fd);

        LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(listen_fd_actual);

        if (client_actual_fd < 0)
        {
            if (errno == EBADF || errno == ENOTSOCK)
                throw ServerError(strerror(errno));
            else
                throw SocketError(strerror(errno));
        }

        LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(client_actual_fd);

#ifdef _USE_LIBEVENT_LIKE_APPLE
        // We set CLOEXEC and unset all other flags to exactly match what
        // happens in Linux with accept4 (see comment to "::accept" above)

        int fcntl_res = fcntl(client_actual_fd, F_SETFD, FD_CLOEXEC);
        if (fcntl_res == -1)
        {
            PS_LOG_DEBUG_ARGS("fcntl F_SETFD fail for fd %d, errno %d %s",
                              client_actual_fd, errno, strerror(errno));

            ::close(client_actual_fd);
            PS_LOG_DEBUG_ARGS("::close actual_fd %d", client_actual_fd);

            return (fcntl_res);
        }

        fcntl_res = fcntl(client_actual_fd, F_SETFL, 0 /*clear everything*/);
        if (fcntl_res == -1)
        {
            PS_LOG_DEBUG_ARGS("fcntl F_SETFL fail for fd %d, errno %d %s",
                              client_actual_fd, errno, strerror(errno));

            ::close(client_actual_fd);
            PS_LOG_DEBUG_ARGS("::close actual_fd %d", client_actual_fd);

            return (fcntl_res);
        }
#endif // ifdef _USE_LIBEVENT_LIKE_APPLE

        LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(client_actual_fd);

        return client_actual_fd;
    }

    void Listener::dispatchPeer(const std::shared_ptr<Peer>& peer)
    {
        PS_TIMEDBG_START_THIS;

        if (!peer)
        {
            PS_LOG_DEBUG("Null peer");
            return;
        }

        // There is some risk that the Fd belonging to the peer could be closed
        // in another thread before this dispatchPeer routine completes. In
        // particular, that has been seen to happen occasionally in
        // rest_server_test.response_status_code_test in OpenBSD.
        //
        // To guard against that, we simply need to check for an invalid Fd. We
        // also check for an invalid actual-fd for safety's sake.

        int actual_fd = -1;
        try
        {
            actual_fd = peer->actualFd();
        }
        catch (...)
        {
            PS_LOG_INFO_ARGS("Failed to get actual fd from peer %p",
                             peer.get());
            return;
        }
        if (actual_fd == -1)
        {
            PS_LOG_INFO_ARGS("No actual fd for peer %p", peer.get());
            return;
        }

        auto handlers  = reactor_->handlers(transportKey);
        auto idx       = actual_fd % handlers.size();
        auto transport = std::static_pointer_cast<Transport>(handlers[idx]);

        transport->handleNewPeer(peer);
    }

    Listener::TransportFactory Listener::defaultTransportFactory() const
    {
        return [&] {
            if (!handler_)
                throw std::runtime_error("setHandler() has not been called");

            return std::make_shared<Transport>(handler_);
        };
    }

#ifdef PISTACHE_USE_SSL

    void Listener::setupSSLAuth(const std::string& ca_file,
                                const std::string& ca_path,
                                int (*cb)(int, void*) = NULL)
    {
        PS_TIMEDBG_START_THIS;

        const char* __ca_file = NULL;
        const char* __ca_path = NULL;

        if (ssl_ctx_ == nullptr)
        {
            PS_LOG_DEBUG("SSL Context is not initialized");

            std::string err = "SSL Context is not initialized";
            PISTACHE_LOG_STRING_FATAL(logger_, err);
            throw std::runtime_error(err);
        }

        if (!ca_file.empty())
            __ca_file = ca_file.c_str();
        if (!ca_path.empty())
            __ca_path = ca_path.c_str();

        if (SSL_CTX_load_verify_locations(GetSSLContext(ssl_ctx_), __ca_file,
                                          __ca_path)
            <= 0)
        {
            std::string err = "SSL error - Cannot verify SSL locations: "
                + ssl_print_errors_to_string();
            PS_LOG_DEBUG_ARGS("%s", err.c_str());

            PISTACHE_LOG_STRING_FATAL(logger_, err);
            throw std::runtime_error(err);
        }

        SSL_CTX_set_verify(GetSSLContext(ssl_ctx_),
                           SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE,
/* Callback type did change in 1.0.1 */
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
                           (int (*)(int, X509_STORE_CTX*))cb
#else
                           (SSL_verify_cb)cb
#endif /* OPENSSL_VERSION_NUMBER */
        );
    }

    void Listener::setupSSL(const std::string& cert_path,
                            const std::string& key_path,
                            bool use_compression,
                            int (*cb_password)(char*, int, int, void*),
                            std::chrono::milliseconds sslHandshakeTimeout)
    {
        SSL_load_error_strings();
        OpenSSL_add_ssl_algorithms();

        try
        {
            ssl_ctx_ = ssl_create_context(cert_path, key_path, use_compression, cb_password);
        }
        catch (std::exception& e)
        {
            PS_LOG_DEBUG("ssl_create_context throw");

            PISTACHE_LOG_STRING_FATAL(logger_, e.what());
            throw;
        }
        sslHandshakeTimeout_ = sslHandshakeTimeout;
        useSSL_              = true;
    }

#endif /* PISTACHE_USE_SSL */

    std::vector<std::shared_ptr<Tcp::Peer>> Listener::getAllPeer()
    {
        std::vector<std::shared_ptr<Tcp::Peer>> vecPeers;
        auto handlers = reactor_->handlers(transportKey);

        for (const auto& handler : handlers)
        {
            auto transport = std::static_pointer_cast<Transport>(handler);
            auto peers     = transport->getAllPeer();
            vecPeers.insert(vecPeers.end(), peers.begin(), peers.end());
        }
        return vecPeers;
    }

} // namespace Pistache::Tcp
