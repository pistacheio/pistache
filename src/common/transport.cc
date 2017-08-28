/* traqnsport.cc
   Mathieu Stefani, 02 July 2017

   TCP transport handling

*/

#include <sys/sendfile.h>
#include <sys/timerfd.h>

#include <pistache/transport.h>
#include <pistache/peer.h>
#include <pistache/tcp.h>
#include <pistache/os.h>


namespace Pistache {

using namespace Polling;

namespace Tcp {

Transport::Transport(const std::shared_ptr<Tcp::Handler>& handler) {
    init(handler);
}

void
Transport::init(const std::shared_ptr<Tcp::Handler>& handler) {
    handler_ = handler;
    handler_->associateTransport(this);
}

std::shared_ptr<Aio::Handler>
Transport::clone() const {
    return std::make_shared<Transport>(handler_->clone());
}

void
Transport::registerPoller(Polling::Epoll& poller) {
    writesQueue.bind(poller);
    timersQueue.bind(poller);
    peersQueue.bind(poller);
    notifier.bind(poller);
}

void
Transport::handleNewPeer(const std::shared_ptr<Tcp::Peer>& peer) {
    auto ctx = context();
    const bool isInRightThread = std::this_thread::get_id() == ctx.thread();
    if (!isInRightThread) {
        PeerEntry entry(peer);
        auto *e = peersQueue.allocEntry(entry);
        peersQueue.push(e);
    } else {
        handlePeer(peer);
    }
}

void
Transport::onReady(const Aio::FdSet& fds) {
    for (const auto& entry: fds) {
        if (entry.getTag() == writesQueue.tag()) {
            handleWriteQueue();
        }
        else if (entry.getTag() == timersQueue.tag()) {
            handleTimerQueue();
        }
        else if (entry.getTag() == peersQueue.tag()) {
            handlePeerQueue();
        }
        else if (entry.getTag() == notifier.tag()) {
            handleNotify();
        }

        else if (entry.isReadable()) {
            auto tag = entry.getTag();
            auto val = tag.value();
            if (isPeerFd(tag)) {
                auto& peer = getPeer(tag);
                handleIncoming(peer);
            } else if (isTimerFd(tag)) {
                auto it = timers.find(tag.value());
                auto& entry = it->second;
                handleTimer(std::move(entry));
                timers.erase(it);
            }
            else {
                throw std::runtime_error("Unknown fd");
            }

        }
        else if (entry.isWritable()) {
            auto tag = entry.getTag();
            auto fd = tag.value();

            auto it = toWrite.find(fd);
            if (it == std::end(toWrite)) {
                throw std::runtime_error("Assertion Error: could not find write data");
            }

            reactor()->modifyFd(key(), fd, NotifyOn::Read, Polling::Mode::Edge);

            auto& write = it->second;
            asyncWriteImpl(fd, write, Retry);
        }
    }
}

void
Transport::disarmTimer(Fd fd) {
    auto it = timers.find(fd);
    if (it == std::end(timers))
        throw std::runtime_error("Timer has not been armed");

    auto &entry = it->second;
    entry.disable();
}

void
Transport::handleIncoming(const std::shared_ptr<Peer>& peer) {
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
                    handlePeerDisconnection(peer);
                }
                else {
                    throw std::runtime_error(strerror(errno));
                }
            }
            break;
        }
        else if (bytes == 0) {
            handlePeerDisconnection(peer);
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
Transport::handlePeerDisconnection(const std::shared_ptr<Peer>& peer) {
    handler_->onDisconnection(peer);

    int fd = peer->fd();
    auto it = peers.find(fd);
    if (it == std::end(peers))
        throw std::runtime_error("Could not find peer to erase");

    peers.erase(it);

    close(fd);
}

void
Transport::asyncWriteImpl(Fd fd, Transport::WriteEntry& entry, WriteStatus status) {
    asyncWriteImpl(fd, entry.flags, entry.buffer, std::move(entry.deferred), status);
}

void
Transport::asyncWriteImpl(
        Fd fd, int flags, const BufferHolder& buffer,
        Async::Deferred<ssize_t> deferred, WriteStatus status)
{
    auto cleanUp = [&]() {
        if (buffer.isRaw()) {
            auto raw = buffer.raw();
            if (raw.isOwned) delete[] raw.data;
        }

        if (status == Retry)
            toWrite.erase(fd);
    };

    ssize_t totalWritten = buffer.offset();
    for (;;) {
        ssize_t bytesWritten = 0;
        auto len = buffer.size() - totalWritten;
        if (buffer.isRaw()) {
            auto raw = buffer.raw();
            auto ptr = raw.data + totalWritten;
            bytesWritten = ::send(fd, ptr, len, flags);
        } else {
            auto file = buffer.fd();
            off_t offset = totalWritten;
            bytesWritten = ::sendfile(fd, file, &offset, len);
        }
        if (bytesWritten < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // save for a future retry with the totalWritten offset.
                if (status == Retry) {
                    toWrite.erase(fd);
                }

                toWrite.insert(
                        std::make_pair(fd,
                            WriteEntry(std::move(deferred), buffer.detach(totalWritten), flags)));

                reactor()->modifyFd(key(), fd, NotifyOn::Read | NotifyOn::Write, Polling::Mode::Edge);
            }
            else {
                cleanUp();
                deferred.reject(Pistache::Error::system("Could not write data"));
            }
            break;
        }
        else {
            totalWritten += bytesWritten;
            if (totalWritten >= buffer.size()) {
                cleanUp();
                deferred.resolve(totalWritten);
                break;
            }
        }
    }
}

void
Transport::armTimerMs(
        Fd fd, std::chrono::milliseconds value,
        Async::Deferred<uint64_t> deferred) {

    auto ctx = context();
    const bool isInRightThread = std::this_thread::get_id() == ctx.thread();
    TimerEntry entry(fd, value, std::move(deferred));

    if (!isInRightThread) {
        auto *e = timersQueue.allocEntry(std::move(entry));
        timersQueue.push(e);
    } else {
        armTimerMsImpl(std::move(entry));
    }
}

void
Transport::armTimerMsImpl(TimerEntry entry) {

    auto it = timers.find(entry.fd);
    if (it != std::end(timers)) {
        entry.deferred.reject(std::runtime_error("Timer is already armed"));
        return;
    }

    itimerspec spec;
    spec.it_interval.tv_sec = 0;
    spec.it_interval.tv_nsec = 0;

    if (entry.value.count() < 1000) {
        spec.it_value.tv_sec = 0;
        spec.it_value.tv_nsec
            = std::chrono::duration_cast<std::chrono::nanoseconds>(entry.value).count();
    } else {
        spec.it_value.tv_sec
            = std::chrono::duration_cast<std::chrono::seconds>(entry.value).count();
        spec.it_value.tv_nsec = 0;
    }

    int res = timerfd_settime(entry.fd, 0, &spec, 0);
    if (res == -1) {
        entry.deferred.reject(Pistache::Error::system("Could not set timer time"));
        return;
    }

    reactor()->registerFdOneShot(key(), entry.fd, NotifyOn::Read, Polling::Mode::Edge);
    timers.insert(std::make_pair(entry.fd, std::move(entry)));
}

void
Transport::handleWriteQueue() {
    // Let's drain the queue
    for (;;) {
        auto entry = writesQueue.popSafe();
        if (!entry) break;

        auto &write = entry->data();
        asyncWriteImpl(write.peerFd, write);
    }
}

void
Transport::handleTimerQueue() {
    for (;;) {
        auto entry = timersQueue.popSafe();
        if (!entry) break;

        auto &timer = entry->data();
        armTimerMsImpl(std::move(timer));
    }
}

void
Transport::handlePeerQueue() {
    for (;;) {
        auto entry = peersQueue.popSafe();
        if (!entry) break;

        const auto &data = entry->data();
        handlePeer(data.peer);
    }
}

void
Transport::handlePeer(const std::shared_ptr<Peer>& peer) {
    int fd = peer->fd();
    peers.insert(std::make_pair(fd, peer));

    peer->associateTransport(this);

    handler_->onConnection(peer);
    reactor()->registerFd(key(), fd, NotifyOn::Read | NotifyOn::Shutdown, Polling::Mode::Edge);
}

void
Transport::handleNotify() {
    while (this->notifier.tryRead()) ;

    rusage now;

    auto res = getrusage(RUSAGE_THREAD, &now);
    if (res == -1)
        loadRequest_.reject(std::runtime_error("Could not compute usage"));

    loadRequest_.resolve(now);
    loadRequest_.clear();
}

void
Transport::handleTimer(TimerEntry entry) {
    if (entry.isActive()) {
        uint64_t numWakeups;
        int res = ::read(entry.fd, &numWakeups, sizeof numWakeups);
        if (res == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            else
                entry.deferred.reject(Pistache::Error::system("Could not read timerfd"));
        } else {
            if (res != sizeof(numWakeups)) {
                entry.deferred.reject(Pistache::Error("Read invalid number of bytes for timer fd: "
                            + std::to_string(entry.fd)));
            }
            else {
                entry.deferred.resolve(numWakeups);
            }
        }
    }
}

bool
Transport::isPeerFd(Fd fd) const {
    return peers.find(fd) != std::end(peers);
}

bool
Transport::isTimerFd(Fd fd) const {
    return timers.find(fd) != std::end(timers);
}

bool
Transport::isPeerFd(Polling::Tag tag) const {
    return isPeerFd(tag.value());
}
bool
Transport::isTimerFd(Polling::Tag tag) const {
    return isTimerFd(tag.value());
}

std::shared_ptr<Peer>&
Transport::getPeer(Fd fd)
{
    auto it = peers.find(fd);
    if (it == std::end(peers))
    {
        throw std::runtime_error("No peer found for fd: " + std::to_string(fd));
    }
    return it->second;
}

std::shared_ptr<Peer>&
Transport::getPeer(Polling::Tag tag)
{
    return getPeer(tag.value());
}

} // namespace Tcp
} // namespace Pistache
