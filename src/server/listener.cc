/* listener.cc
   Mathieu Stefani, 12 August 2015

*/

#include <iostream>
#include <cassert>
#include <cstring>

#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/timerfd.h>
#include <sys/sendfile.h>
#include <cerrno>

#include <pistache/listener.h>
#include <pistache/peer.h>
#include <pistache/common.h>
#include <pistache/os.h>
#include <pistache/transport.h>

using namespace std;

namespace Pistache {
namespace Tcp {

namespace {
    volatile sig_atomic_t g_listen_fd = -1;

    void closeListener() {
        if (g_listen_fd != -1) {
            ::close(g_listen_fd);
            g_listen_fd = -1;
        }
    }

    void handle_sigint(int) {
        closeListener();
    }
}

using Polling::NotifyOn;


void setSocketOptions(Fd fd, Flags<Options> options) {
    if (options.hasFlag(Options::ReuseAddr)) {
        int one = 1;
        TRY(::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof (one)));
    }

    if (options.hasFlag(Options::Linger)) {
        struct linger opt;
        opt.l_onoff = 1;
        opt.l_linger = 1;
        TRY(::setsockopt(fd, SOL_SOCKET, SO_LINGER, &opt, sizeof (opt)));
    }

    if (options.hasFlag(Options::FastOpen)) {
        int hint = 5;
        TRY(::setsockopt(fd, SOL_TCP, TCP_FASTOPEN, &hint, sizeof (hint)));
    }
    if (options.hasFlag(Options::NoDelay)) {
        int one = 1;
        TRY(::setsockopt(fd, SOL_TCP, TCP_NODELAY, &one, sizeof (one)));
    }

}

Listener::Listener()
    : listen_fd(-1)
    , backlog_(Const::MaxBacklog)
    , reactor_(Aio::Reactor::create())
{ }

Listener::Listener(const Address& address)
    : addr_(address)
    , listen_fd(-1)
    , backlog_(Const::MaxBacklog)
    , reactor_(Aio::Reactor::create())
{
}

Listener::~Listener() {
    if (isBound()) shutdown();
    if (acceptThread) acceptThread->join();
}

void
Listener::init(
    size_t workers,
    Flags<Options> options, int backlog)
{
    if (workers > hardware_concurrency()) {
        // Log::warning() << "More workers than available cores"
    }

    options_ = options;
    backlog_ = backlog;

    if (options_.hasFlag(Options::InstallSignalHandler)) {
        if (signal(SIGINT, handle_sigint) == SIG_ERR) {
            throw std::runtime_error("Could not install signal handler");
        }
    }

    workers_ = workers;

}

void
Listener::setHandler(const std::shared_ptr<Handler>& handler) {
    handler_ = handler;
}

void
Listener::pinWorker(size_t worker, const CpuSet& set)
{
    UNUSED(worker)
    UNUSED(set)
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

bool
Listener::bind() {
    return bind(addr_);
}

bool
Listener::bind(const Address& address) {
    if (!handler_)
        throw std::runtime_error("Call setHandler before calling bind()");
    addr_ = address;

    struct addrinfo hints;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;

    const auto& host = addr_.host();
    const auto& port = addr_.port().toString();
    struct addrinfo *addrs;
    TRY(::getaddrinfo(host.c_str(), port.c_str(), &hints, &addrs));

    int fd = -1;

    addrinfo *addr;
    for (addr = addrs; addr; addr = addr->ai_next) {
        fd = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (fd < 0) continue;

        setSocketOptions(fd, options_);

        if (::bind(fd, addr->ai_addr, addr->ai_addrlen) < 0) {
            close(fd);
            continue;
        }

        TRY(::listen(fd, backlog_));
        break;
    }
    
    // At this point, it is still possible that we couldn't bind any socket. If it is the case, the previous
    // loop would have exited naturally and addr will be null.
    if (addr == nullptr) {
        throw std::runtime_error(strerror(errno));
    }

    make_non_blocking(fd);
    poller.addFd(fd, Polling::NotifyOn::Read, Polling::Tag(fd));
    listen_fd = fd;
    g_listen_fd = fd;

    transport_.reset(new Transport(handler_));

    reactor_->init(Aio::AsyncContext(workers_));
    transportKey = reactor_->addHandler(transport_);

    return true;
}

bool
Listener::isBound() const {
    return listen_fd != -1;
}

void
Listener::run() {
    reactor_->run();

    for (;;) {
        std::vector<Polling::Event> events;

        int ready_fds = poller.poll(events, 128, std::chrono::milliseconds(-1));
        if (ready_fds == -1) {
            if (errno == EINTR && g_listen_fd == -1) return;
            throw Error::system("Polling");
        }
        else if (ready_fds > 0) {
            for (const auto& event: events) {
                if (event.tag == shutdownFd.tag())
                    return;
                else {
                    if (event.flags.hasFlag(Polling::NotifyOn::Read)) {
                        auto fd = event.tag.value();
                        if (static_cast<ssize_t>(fd) == listen_fd)
                            handleNewConnection();
                    }
                }
            }
        }
    }
}

void
Listener::runThreaded() {
    shutdownFd.bind(poller);
    acceptThread.reset(new std::thread([=]() { this->run(); }));
}

void
Listener::shutdown() {
    if (shutdownFd.isBound()) shutdownFd.notify();
    reactor_->shutdown();
}

Async::Promise<Listener::Load>
Listener::requestLoad(const Listener::Load& old) {
    auto handlers = reactor_->handlers(transportKey);

    std::vector<Async::Promise<rusage>> loads;
    for (const auto& handler: handlers) {
        auto transport = std::static_pointer_cast<Transport>(handler);
        loads.push_back(transport->load());
    }

    return Async::whenAll(std::begin(loads), std::end(loads)).then([=](const std::vector<rusage>& usages) {

        Load res;
        res.raw = usages;

        if (old.raw.empty()) {
            res.global = 0.0;
            for (size_t i = 0; i < handlers.size(); ++i) res.workers.push_back(0.0);
        } else {

            auto totalElapsed = [](rusage usage) {
                return (usage.ru_stime.tv_sec * 1e6 + usage.ru_stime.tv_usec)
                     + (usage.ru_utime.tv_sec * 1e6 + usage.ru_utime.tv_usec);
            };

            auto now = std::chrono::system_clock::now();
            auto diff = now - old.tick;
            auto tick = std::chrono::duration_cast<std::chrono::microseconds>(diff);
            res.tick = now;

            for (size_t i = 0; i < usages.size(); ++i) {
                auto last = old.raw[i];
                const auto& usage = usages[i];

                auto nowElapsed = totalElapsed(usage);
                auto timeElapsed = nowElapsed - totalElapsed(last);

                auto loadPct = (timeElapsed * 100.0) / tick.count();
                res.workers.push_back(loadPct);
                res.global += loadPct;

            }

            res.global /= usages.size();
        }

        return res;

     }, Async::Throw);
}

Address
Listener::address() const {
    return addr_;
}

Options
Listener::options() const {
    return options_;
}

void
Listener::handleNewConnection() {
    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);
    int client_fd = ::accept(listen_fd, (struct sockaddr *)&peer_addr, &peer_addr_len);
    if (client_fd < 0) {
        throw std::runtime_error(strerror(errno));
    }

    make_non_blocking(client_fd);

    auto peer = make_shared<Peer>(Address::fromUnix((struct sockaddr *)&peer_addr));
    peer->associateFd(client_fd);

    dispatchPeer(peer);
}

void
Listener::dispatchPeer(const std::shared_ptr<Peer>& peer) {
    auto handlers = reactor_->handlers(transportKey);
    auto idx = peer->fd() % handlers.size();
    auto transport = std::static_pointer_cast<Transport>(handlers[idx]);

    transport->handleNewPeer(peer);

}

} // namespace Tcp
} // namespace Pistache
