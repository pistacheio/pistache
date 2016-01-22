/* io.h
   Mathieu Stefani, 05 novembre 2015
   
   I/O handling
*/

#pragma once

#include "mailbox.h"
#include "flags.h"
#include "os.h"
#include "tcp.h"
#include "async.h"
#include "stream.h"
#include "net.h"

#include <thread>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <sys/time.h>
#include <sys/resource.h>

namespace Net {

namespace Tcp {

class Peer;
class Message;
class Handler;

class IoWorker {
public:
    friend class Peer;
    PollableMailbox<Message> mailbox;

    IoWorker();
    ~IoWorker();

    void start(const std::shared_ptr<Handler> &handler, Flags<Options> options);
    void handleNewPeer(const std::shared_ptr<Peer>& peer);

    void pin(const CpuSet& set);

    void shutdown();

    template<typename Duration>
    void armTimer(Duration timeout, Async::Resolver resolve, Async::Rejection reject) {
        armTimerMs(std::chrono::duration_cast<std::chrono::milliseconds>(timeout),
                   std::move(resolve),
                   std::move(reject));
    }

    void disarmTimer();

    Async::Promise<rusage> getLoad() {
        return Async::Promise<rusage>([=](Async::Resolver& resolve, Async::Rejection& reject) {
            this->load = Some(Load(std::move(resolve), std::move(reject)));
            this->notifier.notify();
        });
    }

    template<typename Buf>
    Async::Promise<ssize_t> asyncWrite(Fd fd, const Buf& buffer, int flags = 0) {
        // If the I/O operation has been initiated from an other thread, we queue it and we'll process
        // it in our own thread so that we make sure that every I/O operation happens in the right thread
        if (std::this_thread::get_id() != thisId) {
            return Async::Promise<ssize_t>([=](Async::Resolver& resolve, Async::Rejection& reject) {
                BufferHolder holder(buffer);
                auto detached = holder.detach();
                OnHoldWrite write(std::move(resolve), std::move(reject), detached, flags);
                write.peerFd = fd;
                auto *e = writesQueue.allocEntry(write);
                writesQueue.push(e);
            });
        }
        return Async::Promise<ssize_t>([&](Async::Resolver& resolve, Async::Rejection& reject) {

            auto it = toWrite.find(fd);
            if (it != std::end(toWrite)) {
                reject(Net::Error("Multiple writes on the same fd"));
                return;
            }

            asyncWriteImpl(fd, flags, BufferHolder(buffer), resolve, reject);

        });
    }

private:

    void
    armTimerMs(std::chrono::milliseconds value, Async::Resolver, Async::Rejection reject);

    struct Timer {
        Timer(std::chrono::milliseconds value,
                Async::Resolver resolve,
                Async::Rejection reject)
          : value(value)
          , resolve(std::move(resolve))
          , reject(std::move(reject))  {
        } 

        std::chrono::milliseconds value;

        Async::Resolver resolve;
        Async::Rejection reject;
    };

    struct Load {
        Load(Async::Resolver resolve,
             Async::Rejection reject)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
        { }

        Async::Resolver resolve;
        Async::Rejection reject;
    };

    enum WriteStatus {
        FirstTry,
        Retry
    };

    struct BufferHolder {
        enum Type { Raw, File };

        explicit BufferHolder(const Buffer& buffer)
            : type(Raw)
            , u(buffer)
        {
            size_ = buffer.len;
        }

        explicit BufferHolder(const FileBuffer& buffer)
            : type(File)
            , u(buffer.fd())
        {
            size_ = buffer.size();
        }

        bool isFile() const { return type == File; }
        bool isRaw() const { return type == Raw; }
        size_t size() const { return size_; }

        Fd fd() const {
            if (!isFile())
                throw std::runtime_error("Tried to retrieve fd of a non-filebuffer");

            return u.fd;

        }

        Buffer raw() const {
            if (!isRaw())
                throw std::runtime_error("Tried to retrieve raw data of a non-buffer");

            return u.raw;
        }

        BufferHolder detach(size_t offset = 0) const {
            if (!isRaw())
                return BufferHolder(u.fd, size_);

            if (u.raw.isOwned)
                return BufferHolder(u.raw);

            auto detached = u.raw.detach(offset);
            return BufferHolder(detached);
        }

    private:
        BufferHolder(Fd fd, size_t size)
         : u(fd)
         , size_(size)
         , type(File)
        { }

        union U {
            Buffer raw;
            Fd fd;

            U(Buffer buffer) : raw(buffer) { }
            U(Fd fd) : fd(fd) { }
        } u;
        size_t size_;
        Type type;
    };

    struct OnHoldWrite {
        OnHoldWrite(Async::Resolver resolve, Async::Rejection reject,
                    BufferHolder buffer, int flags = 0)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
            , buffer(std::move(buffer))
            , flags(flags)
            , peerFd(-1)
        { }

        Async::Resolver resolve;
        Async::Rejection reject;

        BufferHolder buffer;
        int flags;
        Fd peerFd;
    };

    Polling::Epoll poller;
    std::unique_ptr<std::thread> thread;
    mutable std::mutex peersMutex;
    std::unordered_map<Fd, std::shared_ptr<Peer>> peers;
    /* @Incomplete: this should be a std::dequeue.
        If an asyncWrite on a particular fd is initiated whereas the fd is not write-ready
        yet and some writes are still on-hold, writes should queue-up so that when the
        fd becomes ready again, we can write everything
    */
    std::unordered_map<Fd, OnHoldWrite> toWrite;

    Optional<Timer> timer;
    Optional<Load> load;
    Fd timerFd;

    NotifyFd notifier;

    std::shared_ptr<Handler> handler_;
    Flags<Options> options_;

    CpuSet pins;

    std::thread::id thisId;
    PollableQueue<OnHoldWrite> writesQueue;

    std::shared_ptr<Peer>& getPeer(Fd fd);
    std::shared_ptr<Peer>& getPeer(Polling::Tag tag);

    void asyncWriteImpl(Fd fd, const OnHoldWrite& entry, WriteStatus status = FirstTry);
    void asyncWriteImpl(
            Fd fd, int flags, const BufferHolder& buffer,
            Async::Resolver resolve, Async::Rejection reject,
            WriteStatus status = FirstTry);

    void handlePeerDisconnection(const std::shared_ptr<Peer>& peer);

    void handleIncoming(const std::shared_ptr<Peer>& peer);
    void handleTimeout();
    void handleNotify();
    void handleWriteQueue();
    void run();

};

} // namespace Tcp

} // namespace Net

