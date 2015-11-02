/* listener.cc
   Mathieu Stefani, 12 August 2015
   
*/

#include <thread>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h> 
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <signal.h>
#include <cassert>
#include <cstring>
#include "listener.h"
#include "peer.h"
#include "common.h"
#include "os.h"

using namespace std;

namespace Net {

namespace Tcp {

namespace {
    volatile sig_atomic_t g_listen_fd = -1;

    void handle_sigint(int) {
        if (g_listen_fd != -1) {
            close(g_listen_fd);
            g_listen_fd = -1;
        }
    }
}

using Polling::NotifyOn;

struct Message {
    virtual ~Message() { }

    enum class Type { Shutdown };

    virtual Type type() const = 0;
};

struct ShutdownMessage : public Message {
    Type type() const { return Type::Shutdown; }
};

template<typename To>
To *message_cast(const std::unique_ptr<Message>& from)
{
    return static_cast<To *>(from.get());
}

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

IoWorker::IoWorker() {
}

IoWorker::~IoWorker() {
    if (thread && thread->joinable()) {
        thread->join();
    }
}

void
IoWorker::start(const std::shared_ptr<Handler>& handler, Flags<Options> options) {
    handler_ = handler;
    options_ = options;

    thread.reset(new std::thread([this]() {
        this->run();
    }));

    if (pins.count() > 0) {
        auto cpuset = pins.toPosix();
        auto handle = thread->native_handle();
        pthread_setaffinity_np(handle, sizeof (cpuset), &cpuset);
    }
}

void
IoWorker::pin(const CpuSet& set) {
    pins = set;

    if (thread) {
        auto cpuset = set.toPosix();
        auto handle = thread->native_handle();
        pthread_setaffinity_np(handle, sizeof (cpuset), &cpuset);
    }
}

std::shared_ptr<Peer>&
IoWorker::getPeer(Fd fd)
{
    std::unique_lock<std::mutex> guard(peersMutex);
    auto it = peers.find(fd);
    if (it == std::end(peers))
    {
        throw std::runtime_error("No peer found for fd: " + to_string(fd));
    }
    return it->second;
}

std::shared_ptr<Peer>&
IoWorker::getPeer(Polling::Tag tag)
{
    return getPeer(tag.value());
}

void
IoWorker::handleIncoming(const std::shared_ptr<Peer>& peer) {

    char buffer[Const::MaxBuffer];
    memset(buffer, 0, sizeof buffer);

    ssize_t totalBytes = 0;
    int fd = peer->fd();

    for (;;) {

        ssize_t bytes;

        bytes = recv(fd, buffer + totalBytes, Const::MaxBuffer - totalBytes, 0);
        if (bytes == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (totalBytes > 0) {
                    handler_->onInput(buffer, totalBytes, peer);
                }
            } else {
                if (errno == ECONNRESET) {
                    handler_->onDisconnection(peer);
                    close(fd);
                }
                else {
                    throw std::runtime_error(strerror(errno));
                }
            }
            break;
        }
        else if (bytes == 0) {
            handler_->onDisconnection(peer);
            close(fd);
            break;
        }

        else {
            totalBytes += bytes;
            if (totalBytes >= Const::MaxBuffer) {
                cerr << "Too long packet" << endl;
                break;
            }
        }
    }

} 

void
IoWorker::handleNewPeer(const std::shared_ptr<Peer>& peer)
{
    int fd = peer->fd();
    {
        std::unique_lock<std::mutex> guard(peersMutex);
        peers.insert(std::make_pair(fd, peer));
    }

    handler_->onConnection(peer);
    poller.addFd(fd, NotifyOn::Read, Polling::Tag(fd), Polling::Mode::Edge);
}


void
IoWorker::run() {

    if (pins.count() > 0) {
    }

    mailbox.bind(poller);

    std::chrono::milliseconds timeout(-1);

    for (;;) {
        std::vector<Polling::Event> events;

        int ready_fds;
        switch(ready_fds = poller.poll(events, 1024, timeout)) {
        case -1:
            break;
        case 0:
            timeout = std::chrono::milliseconds(-1);
            break;
        default:
            for (const auto& event: events) {
                if (event.tag == mailbox.tag()) {
                    std::unique_ptr<Message> msg(mailbox.clear());
                    if (msg->type() == Message::Type::Shutdown) {
                        return;
                    }
                } else {
                    if (event.flags.hasFlag(NotifyOn::Read)) {
                        auto& peer = getPeer(event.tag);
                        handleIncoming(peer);
                    }
                }
            }
            timeout = std::chrono::milliseconds(0);
            break;
        }
    }
}

void
IoWorker::handleMailbox() {
}

Handler::Handler()
{ }

Handler::~Handler()
{ }

void
Handler::onConnection(const std::shared_ptr<Tcp::Peer>& peer) {
}

void
Handler::onDisconnection(const std::shared_ptr<Tcp::Peer>& peer) {
}

Listener::Listener()
    : listen_fd(-1)
{ }

Listener::Listener(const Address& address)
    : addr_(address)
    , listen_fd(-1)
{
}


void
Listener::init(size_t workers, Flags<Options> options)
{
    if (workers > hardware_concurrency()) {
        // Log::warning() << "More workers than available cores"
    }

    options_ = options;

    if (options_.hasFlag(Options::InstallSignalHandler)) {
        if (signal(SIGINT, handle_sigint) == SIG_ERR) {
            throw std::runtime_error("Could not install signal handler");
        }
    }

    for (size_t i = 0; i < workers; ++i) {
        auto wrk = std::unique_ptr<IoWorker>(new IoWorker);
        ioGroup.push_back(std::move(wrk));
    }
}

void
Listener::setHandler(const std::shared_ptr<Handler>& handler)
{
    handler_ = handler;
}

void
Listener::pinWorker(size_t worker, const CpuSet& set)
{
    if (ioGroup.empty()) {
        throw std::domain_error("Invalid operation, did you call init() before ?");
    }
    if (worker > ioGroup.size()) {
        throw std::invalid_argument("Trying to pin invalid worker");
    }

    auto &wrk = ioGroup[worker];
    wrk->pin(set);
}

bool
Listener::bind() {
    return bind(addr_);
}

bool
Listener::bind(const Address& address) {
    if (ioGroup.empty()) {
        throw std::runtime_error("Call init() before calling bind()");
    }

    addr_ = address;

    struct addrinfo hints;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM; 
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;

    auto host = addr_.host();
    if (host == "*") {
        host = "0.0.0.0";
    }

    /* We rely on the fact that a string literal is an lvalue const char[N] */
    static constexpr size_t MaxPortLen = sizeof("65535");

    char port[MaxPortLen];
    std::fill(port, port + MaxPortLen, 0);
    std::snprintf(port, MaxPortLen, "%d", static_cast<uint16_t>(addr_.port()));

    struct addrinfo *addrs;
    TRY(::getaddrinfo(host.c_str(), port, &hints, &addrs));

    int fd = -1;

    for (struct addrinfo *addr = addrs; addr; addr = addr->ai_next) {
        fd = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (fd < 0) continue;

        setSocketOptions(fd, options_);

        if (::bind(fd, addr->ai_addr, addr->ai_addrlen) < 0) {
            close(fd);
            continue;
        }

        TRY(::listen(fd, Const::MaxBacklog));
    }


    listen_fd = fd;
    g_listen_fd = fd;

    for (auto& io: ioGroup) {
        io->start(handler_, options_);
    }
    
    return true;
}

void
Listener::run() {
    for (;;) {
        struct sockaddr_in peer_addr;
        socklen_t peer_addr_len = sizeof(peer_addr);
        int client_fd = ::accept(listen_fd, (struct sockaddr *)&peer_addr, &peer_addr_len);
        if (client_fd < 0) {
            if (g_listen_fd == -1) {
                cout << "SIGINT Signal received, shutdowning !" << endl;
                shutdown();
                break;

            } else {
                throw std::runtime_error(strerror(errno));
            }
        }

        make_non_blocking(client_fd);

        auto peer = make_shared<Peer>(Address::fromUnix((struct sockaddr *)&peer_addr));
        peer->associateFd(client_fd);

        dispatchPeer(peer);
    }
}

void
Listener::shutdown() {
    for (auto &worker: ioGroup) {
        worker->mailbox.post(new ShutdownMessage());
    }
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
Listener::dispatchPeer(const std::shared_ptr<Peer>& peer) {
    const size_t workers = ioGroup.size();
    size_t worker = peer->fd() % workers;

    ioGroup[worker]->handleNewPeer(peer);

}

} // namespace Tcp

} // namespace Net
