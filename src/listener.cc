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

using namespace std;

namespace Net {

namespace Tcp {

IoWorker::IoWorker() {
    epoll_fd = TRY_RET(epoll_create(128));
}

IoWorker::~IoWorker() {
    if (thread && thread->joinable()) {
        thread->join();
    }
}

void
IoWorker::start() {
    thread.reset(new std::thread([this]() {
        this->run();
    }));
}


void
IoWorker::readIncomingData(int fd) {

    char buffer[Const::MaxBuffer];
    memset(buffer, 0, sizeof buffer);

    ssize_t totalBytes = 0;

    for (;;) {

        ssize_t bytes;

        bytes = recv(fd, buffer + totalBytes, Const::MaxBuffer - totalBytes, 0);
        if (bytes == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                cout << "Received " << buffer << endl;
            } else {
                throw std::runtime_error(strerror(errno));
            }
            break;
        }
        else if (bytes == 0) {
            cout << "Peer has been shutdowned" << endl;
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
IoWorker::run() {
    struct epoll_event events[Const::MaxEvents];

    for (;;) {
        int ready_fds;
        switch(ready_fds = epoll_wait(epoll_fd, events, Const::MaxEvents, 100)) {
        case -1:
            break;
        case 0:
            if (!mailbox.isEmpty()) {
                int *fd = mailbox.clear();

                struct epoll_event event;
                event.events = EPOLLIN;
                event.data.fd = *fd;

                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, *fd, &event);

                delete fd;
            }
            break;
        default:
            for (int i = 0; i < ready_fds; ++i) {
                const struct epoll_event *event = events + i;
                readIncomingData(event->data.fd);
            }
            break;

        }
    }
}

Listener::Listener(const Address& address)
    : addr_(address)
    , listen_fd(-1)
{
}

bool Listener::bind() {

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
    std::snprintf(port, MaxPortLen, "%d", addr_.port());

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

    for (int i = 0; i < 4; ++i) {
        auto wrk = std::unique_ptr<IoWorker>(new IoWorker);
        wrk->start();
        ioGroup.push_back(std::move(wrk));
    }
    
    return true;
}

void Listener::run() {
    for (;;) {
        struct sockaddr_in peer_addr;
        socklen_t peer_addr_len = sizeof(peer_addr);
        int client_fd = TRY_RET(::accept(listen_fd, (struct sockaddr *)&peer_addr, &peer_addr_len));

        make_non_blocking(client_fd);

        char peer_host[NI_MAXHOST];

        if (getnameinfo((struct sockaddr *)&peer_addr, peer_addr_len, peer_host, NI_MAXHOST, nullptr, 0, 0) == 0) {
            Address addr = Address::fromUnix((struct sockaddr *)&peer_addr);
            Peer peer(addr, peer_host);

            std::cout << "New peer: " << peer << std::endl;

            dispatchConnection(client_fd);
        }

    }
}


void Listener::dispatchConnection(int fd) {
    size_t start = fd % 4;
    /* Find the first available worker */
    size_t current = start;
    for (;;) {
        auto& mailbox = ioGroup[current]->mailbox;
        if (mailbox.isEmpty()) {
            int *message = new int(fd);
            int *old = mailbox.post(message);
            assert(old == nullptr);
            return;
        }

        current = (current + 1) % 4;
        if (current == start) {
            break;
        } 
    }

    /* We did not find any available worker, what do we do ? */

}

} // namespace Tcp

} // namespace Net
