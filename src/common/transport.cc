#include "transport.h"
#include "peer.h"
#include "tcp.h"
#include "os.h"
#include <sys/sendfile.h>

using namespace Polling;

namespace Net {

namespace Tcp {

Transport::Transport(const std::shared_ptr<Tcp::Handler>& handler) {
    init(handler);
}

void
Transport::init(const std::shared_ptr<Tcp::Handler>& handler) {
    handler_ = handler;
    handler_->associateTransport(this);
}

std::shared_ptr<Io::Handler>
Transport::clone() const {
    return std::make_shared<Transport>(handler_->clone());
}

void
Transport::registerPoller(Polling::Epoll& poller) {
    writesQueue.bind(poller);
}

void
Transport::handleNewPeer(const std::shared_ptr<Tcp::Peer>& peer) {
    int fd = peer->fd();
    {
        std::unique_lock<std::mutex> guard(peersMutex);
        peers.insert(std::make_pair(fd, peer));
    }

    peer->associateTransport(this);

    handler_->onConnection(peer);
    io()->registerFd(fd, NotifyOn::Read | NotifyOn::Shutdown, Polling::Mode::Edge);
}

void
Transport::onReady(const Io::FdSet& fds) {
    for (const auto& entry: fds) {
        if (entry.getTag() == writesQueue.tag()) {
            handleWriteQueue();
        }

        else if (entry.isReadable()) {
            auto& peer = getPeer(entry.getTag());
            handleIncoming(peer);
        }
        else if (entry.isWritable()) {
            auto tag = entry.getTag();
            auto fd = tag.value();

            auto it = toWrite.find(fd);
            if (it == std::end(toWrite)) {
                throw std::runtime_error("Assertion Error: could not find write data");
            }

            io()->modifyFd(fd, NotifyOn::Read, Polling::Mode::Edge);

            auto& write = it->second;
            asyncWriteImpl(fd, write, Retry);
        }
    }
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
    {
        std::unique_lock<std::mutex> guard(peersMutex);
        auto it = peers.find(fd);
        if (it == std::end(peers))
            throw std::runtime_error("Could not find peer to erase");

        peers.erase(it);
    }

    close(fd);
}

void
Transport::asyncWriteImpl(Fd fd, Transport::OnHoldWrite& entry, WriteStatus status) {
    asyncWriteImpl(fd, entry.flags, entry.buffer, std::move(entry.resolve), std::move(entry.reject), status);
}

void
Transport::asyncWriteImpl(
        Fd fd, int flags, const BufferHolder& buffer,
        Async::Resolver resolve, Async::Rejection reject, WriteStatus status)
{
    auto cleanUp = [&]() {
        if (buffer.isRaw()) {
            auto raw = buffer.raw();
            if (raw.isOwned) delete[] raw.data;
        }

        if (status == Retry)
            toWrite.erase(fd);
    };

    ssize_t totalWritten = 0;
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
                if (status == FirstTry) {
                    toWrite.insert(
                            std::make_pair(fd,
                                OnHoldWrite(std::move(resolve), std::move(reject), buffer.detach(totalWritten), flags)));
                }
                io()->modifyFd(fd, NotifyOn::Read | NotifyOn::Write, Polling::Mode::Edge);
            }
            else {
                cleanUp();
                reject(Net::Error::system("Could not write data"));
            }
            break;
        }
        else {
            totalWritten += bytesWritten;
            if (totalWritten == len) {
                cleanUp();
                resolve(totalWritten);
                break;
            }
        }
    }
}

void
Transport::handleWriteQueue() {
    // Let's drain the queue
    for (;;) {
        std::unique_ptr<PollableQueue<OnHoldWrite>::Entry> entry(writesQueue.pop());
        if (!entry) break;

        auto &write = entry->data();
        asyncWriteImpl(write.peerFd, write);
    }
}

std::shared_ptr<Peer>&
Transport::getPeer(Fd fd)
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
Transport::getPeer(Polling::Tag tag)
{
    return getPeer(tag.value());
}

} // namespace Tcp

} // namespace Net
