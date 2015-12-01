/* io.cc
   Mathieu Stefani, 05 novembre 2015
   
   I/O handling
*/

#include <thread>
#include "io.h"
#include "listener.h"
#include "peer.h"
#include "os.h"
#include <sys/timerfd.h>

namespace Net {

namespace Tcp {

using namespace Polling;

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

IoWorker::IoWorker() {
    timerFd = TRY_RET(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));
    poller.addFd(timerFd, Polling::NotifyOn::Read, Polling::Tag(timerFd));
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

void
IoWorker::setTimeoutMs(
        std::chrono::milliseconds value,
        Async::Resolver resolve, Async::Rejection reject)
{
    itimerspec spec;
    spec.it_interval.tv_sec = 0;
    spec.it_interval.tv_nsec = 0;

    if (value.count() < 1000) {
        spec.it_value.tv_sec = 0;
        spec.it_value.tv_nsec
            = std::chrono::duration_cast<std::chrono::nanoseconds>(value).count();
    } else {
        spec.it_value.tv_sec
            = std::chrono::duration_cast<std::chrono::seconds>(value).count();
        spec.it_value.tv_nsec = 0;
    }


    int res = timerfd_settime(timerFd, 0, &spec, 0);
    if (res == -1) {
        reject(Error::system("Could not set timer time"));
        return;
    }

    timeout = Some(Timeout(value, std::move(resolve), std::move(reject)));

}

void
IoWorker::shutdown() {
    mailbox.post(new ShutdownMessage());
}

std::shared_ptr<Peer>&
IoWorker::getPeer(Fd fd)
{
    std::unique_lock<std::mutex> guard(peersMutex);
    auto it = peers.find(fd);
    if (it == std::end(peers))
    {
        throw std::runtime_error("No peer found for fd: " + std::to_string(fd));
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
                std::cerr << "Too long packet" << std::endl;
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

    peer->io_ = this;

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
                        auto fd = event.tag.value();
                        if (fd == timerFd) {
                            handleTimeout();
                        } else {
                            auto& peer = getPeer(event.tag);
                            handleIncoming(peer);
                        }
                    }
                    else if (event.flags.hasFlag(NotifyOn::Write)) {
                        auto fd = event.tag.value();
                        auto it = toWrite.find(fd);
                        if (it == std::end(toWrite)) {
                            throw std::runtime_error("Assertion Error: could not find write data");
                        }

                        auto &write = it->second;
                        ssize_t bytes = ::send(fd, write.buf, write.len, 0);
                        if (bytes < 0) {
                            write.reject(Net::Error::system("Could not write data"));
                        }

                        else if (bytes < write.len) {
                            write.reject(Net::Error("Failed to write: could not write all bytes"));
                        }
                        else {
                            write.resolve(bytes);
                        }
                    }
                }
            }
            timeout = std::chrono::milliseconds(0);
            break;
        }
    }
}

void
IoWorker::handleTimeout() {

    auto& entry = timeout.unsafeGet();

    uint64_t numWakeups;
    int res = ::read(timerFd, &numWakeups, sizeof numWakeups);
    if (res == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        else
            entry.reject(Error::system("Could not read timerfd"));
    } else {
        if (res != sizeof(numWakeups)) {
            entry.reject(Error("Read invalid number of bytes for timer fd: "
                        + std::to_string(timerFd)));
        }
        else {
            entry.resolve(numWakeups);
        }
    }
}

Async::Promise<ssize_t>
IoWorker::asyncWrite(Fd fd, const void* buf, size_t len) {
    return Async::Promise<ssize_t>([=](Async::Resolver& resolve, Async::Rejection& reject) {

        auto it = toWrite.find(fd);
        if (it != std::end(toWrite)) {
            reject(Net::Error("Multiple writes on the same fd"));
            return;
        }

        ssize_t totalWritten = 0;
        for (;;) {
            auto *bufPtr = static_cast<const char *>(buf) + totalWritten;
            auto bufLen = len - totalWritten;
            ssize_t bytesWritten = ::send(fd, bufPtr, bufLen, 0);
            if (bytesWritten < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    toWrite.insert(
                            std::make_pair(fd, 
                                OnHoldWrite(std::move(resolve), std::move(reject), bufPtr, bufLen)));
                    poller.addFdOneShot(fd, NotifyOn::Write, Polling::Tag(fd));
                }
                else {
                    reject(Net::Error::system("Could not write data"));
                }
                break;
            }
            else {
                totalWritten += bytesWritten;
                if (totalWritten == len) {
                    resolve(totalWritten);
                    break;
                }
            }
        }
    });
}


} // namespace Tcp

} // namespace Net
