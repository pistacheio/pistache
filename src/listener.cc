/* listener.cc
   Mathieu Stefani, 12 August 2015
   
*/

#include <thread>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <cassert>
#include <cstring>
#include "listener.h"
#include "peer.h"
#include "common.h"
#include "os.h"

using namespace std;

namespace Net {

namespace Tcp {

struct Message {
    virtual ~Message() { }

    enum class Type { NewPeer, Shutdown };

    virtual Type type() const = 0;
};

struct PeerMessage : public Message {
    PeerMessage(const std::shared_ptr<Peer>& peer)
        : peer_(peer)
    { }

    Type type() const { return Type::NewPeer; }
    std::shared_ptr<Peer> peer() const { return peer_; }

private:
    std::shared_ptr<Peer> peer_;
};

template<typename To>
To *message_cast(const std::unique_ptr<Message>& from)
{
    return static_cast<To *>(from.get());
}


IoWorker::IoWorker() {
    epoll_fd = TRY_RET(epoll_create(128));
}

IoWorker::~IoWorker() {
    if (thread && thread->joinable()) {
        thread->join();
    }
}

void
IoWorker::start(const std::shared_ptr<Handler>& handler) {
    handler_ = handler;
    thread.reset(new std::thread([this]() {
        this->run();
    }));
}

std::shared_ptr<Peer>
IoWorker::getPeer(Fd fd) const
{
    auto it = peers.find(fd);
    if (it == std::end(peers))
    {
        throw std::runtime_error("No peer found for fd: " + to_string(fd));
    }
    return it->second;
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
                handler_->onInput(buffer, totalBytes, *peer);
            } else {
                throw std::runtime_error(strerror(errno));
            }
            break;
        }
        else if (bytes == 0) {
            cout << "Peer " << *peer << " has disconnected" << endl;
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
    std::cout << "New peer: " << *peer << std::endl;
    int fd = peer->fd();

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fd;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    peers.insert(std::make_pair(fd, peer));
}


void
IoWorker::run() {
    struct epoll_event events[Const::MaxEvents];

    for (;;) {
        int ready_fds;
        switch(ready_fds = epoll_wait(epoll_fd, events, Const::MaxEvents, 100)) {
        case -1:
            break;
        case 0:
            if (!mailbox.isEmpty()) {
                std::unique_ptr<Message> msg(mailbox.clear());
                if (msg->type() == Message::Type::NewPeer) {
                    auto peer_msg = message_cast<PeerMessage>(msg);

                    handleNewPeer(peer_msg->peer());
                }

            }
            break;
        default:
            for (int i = 0; i < ready_fds; ++i) {
                const struct epoll_event *event = events + i;
                handleIncoming(getPeer(event->data.fd));
            }
            break;

        }
    }
}

Handler::Handler()
{ }

Handler::~Handler()
{ }

void
Handler::onConnection() {
}

void
Handler::onDisconnection() {
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
Listener::init(size_t workers)
{
    if (workers > hardware_concurrency()) {
        // Log::warning() << "More workers than available cores"
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

        if (::bind(fd, addr->ai_addr, addr->ai_addrlen) < 0) {
            close(fd);
            continue;
        }

        TRY(::listen(fd, Const::MaxBacklog));
    }

    listen_fd = fd;

    for (auto& io: ioGroup) {
        io->start(handler_);
    }
    
    return true;
}

void
Listener::run() {
    for (;;) {
        struct sockaddr_in peer_addr;
        socklen_t peer_addr_len = sizeof(peer_addr);
        int client_fd = TRY_RET(::accept(listen_fd, (struct sockaddr *)&peer_addr, &peer_addr_len));

        make_non_blocking(client_fd);

        char peer_host[NI_MAXHOST];

        if (getnameinfo((struct sockaddr *)&peer_addr, peer_addr_len, peer_host, NI_MAXHOST, nullptr, 0, 0) == 0) {
            Address addr = Address::fromUnix((struct sockaddr *)&peer_addr);
            auto peer = make_shared<Peer>(addr, peer_host);
            peer->associateFd(client_fd);

            dispatchPeer(peer);
        }

    }
}

Address
Listener::address() const {
    return addr_;
}


void
Listener::dispatchPeer(const std::shared_ptr<Peer>& peer) {
    const size_t workers = ioGroup.size();
    size_t start = peer->fd() % workers;

    /* Find the first available worker */
    size_t current = start;
    for (;;) {
        auto& mailbox = ioGroup[current]->mailbox;

        if (mailbox.isEmpty()) {
            auto message = new PeerMessage(peer);

            auto *old = mailbox.post(message);
            assert(old == nullptr);
            return;
        }

        current = (current + 1) % workers;
        if (current == start) {
            break;
        } 
    }

    /* We did not find any available worker, what do we do ? */

}

} // namespace Tcp

} // namespace Net
