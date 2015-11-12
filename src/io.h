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

    Polling::Epoll poller;
    std::unique_ptr<std::thread> thread;
    mutable std::mutex peersMutex;
    std::unordered_map<Fd, std::shared_ptr<Peer>> peers;
    std::unordered_map<Fd, OnHoldWrite> toWrite;
    std::shared_ptr<Handler> handler_;
    Flags<Options> options_;

    CpuSet pins;

    std::shared_ptr<Peer>& getPeer(Fd fd);
    std::shared_ptr<Peer>& getPeer(Polling::Tag tag);

    Async::Promise<ssize_t> asyncWrite(Fd fd, const void *buf, size_t len);

    void handleIncoming(const std::shared_ptr<Peer>& peer);
    void run();

};

} // namespace Tcp

} // namespace Net

