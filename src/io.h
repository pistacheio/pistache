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

private:
    struct OnHoldWrite {
        OnHoldWrite(Async::Resolver resolve, Async::Rejection reject,
                    Buffer buffer)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
            , buffer(std::move(buffer))
            , fd(-1)
        { }

        Async::Resolver resolve;
        Async::Rejection reject;

        Buffer buffer;
        Fd fd;
    };

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

    Polling::Epoll poller;
    std::unique_ptr<std::thread> thread;
    mutable std::mutex peersMutex;
    std::unordered_map<Fd, std::shared_ptr<Peer>> peers;
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

    Async::Promise<ssize_t> asyncWrite(Fd fd, const Buffer& buffer);

    void asyncWriteImpl(Fd fd, const OnHoldWrite& entry, WriteStatus status = FirstTry);
    void asyncWriteImpl(
            Fd fd, const Buffer& buffer,
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

