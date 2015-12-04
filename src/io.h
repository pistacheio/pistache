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

#include <thread>
#include <mutex>
#include <memory>
#include <unordered_map>

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

private:
    struct OnHoldWrite {
        OnHoldWrite(Async::Resolver resolve, Async::Rejection reject,
                    const void *buf, size_t len)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
            , buf(buf)
            , len(len)
        { }

        Async::Resolver resolve;
        Async::Rejection reject;

        const void *buf;
        size_t len;   
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

    Polling::Epoll poller;
    std::unique_ptr<std::thread> thread;
    mutable std::mutex peersMutex;
    std::unordered_map<Fd, std::shared_ptr<Peer>> peers;
    std::unordered_map<Fd, OnHoldWrite> toWrite;

    Optional<Timer> timer;
    Fd timerFd;

    std::shared_ptr<Handler> handler_;
    Flags<Options> options_;

    CpuSet pins;

    std::shared_ptr<Peer>& getPeer(Fd fd);
    std::shared_ptr<Peer>& getPeer(Polling::Tag tag);

    Async::Promise<ssize_t> asyncWrite(Fd fd, const void *buf, size_t len);

    void handlePeerDisconnection(const std::shared_ptr<Peer>& peer);

    void handleIncoming(const std::shared_ptr<Peer>& peer);
    void handleTimeout();
    void run();

};

} // namespace Tcp

} // namespace Net

