/* listener.h
   Mathieu Stefani, 12 August 2015
   
  A TCP Listener
*/

#pragma once

#include "net.h"
#include "mailbox.h"
#include "os.h"
#include "flags.h"
#include <vector>
#include <memory>
#include <thread>
#include <unordered_map>
#include <mutex>

namespace Net {

namespace Tcp {

class Peer;
class Message;
class Handler;

enum class Options : uint64_t {
    None                 = 0,
    NoDelay              = 1,
    Linger               = NoDelay << 1,
    FastOpen             = Linger << 1,
    QuickAck             = FastOpen << 1,
    ReuseAddr            = QuickAck << 1,
    ReverseLookup        = ReuseAddr << 1,
    InstallSignalHandler = ReverseLookup << 1
};

DECLARE_FLAGS_OPERATORS(Options)

void setSocketOptions(Fd fd, Flags<Options> options);

class IoWorker {
public:
    PollableMailbox<Message> mailbox;

    IoWorker();
    ~IoWorker();

    void start(const std::shared_ptr<Handler> &handler, Flags<Options> options);
    void handleNewPeer(const std::shared_ptr<Peer>& peer);

    void pin(const CpuSet& set);

private:
    Polling::Epoll poller;
    std::unique_ptr<std::thread> thread;
    mutable std::mutex peersMutex;
    std::unordered_map<Fd, std::shared_ptr<Peer>> peers;
    std::shared_ptr<Handler> handler_;
    Flags<Options> options_;

    CpuSet pins;

    std::shared_ptr<Peer>& getPeer(Fd fd);
    std::shared_ptr<Peer>& getPeer(Polling::Tag tag);

    void handleIncoming(const std::shared_ptr<Peer>& peer);
    void handleMailbox();
    void run();

};

class Listener {
public:

    friend class IoWorker;
    friend class Peer;

    Listener();

    Listener(const Address& address);
    void init(
            size_t workers, Flags<Options> options = Options::None,
            int backlog = Const::MaxBacklog);
    void setHandler(const std::shared_ptr<Handler>& handler);

    bool bind();
    bool bind(const Address& adress);

    void run();
    void shutdown();

    Options options() const;
    Address address() const;

    void pinWorker(size_t worker, const CpuSet& set);

private: 
    Address addr_; 
    int listen_fd;
    int backlog_;
    std::vector<std::unique_ptr<IoWorker>> ioGroup;
    Flags<Options> options_;
    std::shared_ptr<Handler> handler_;

    void dispatchPeer(const std::shared_ptr<Peer>& peer);
};

class Handler {
public:
    Handler();
    ~Handler();

    virtual void onInput(const char *buffer, size_t len, const std::shared_ptr<Tcp::Peer>& peer) = 0;
    virtual void onOutput() = 0;

    virtual void onConnection(const std::shared_ptr<Tcp::Peer>& peer);
    virtual void onDisconnection(const std::shared_ptr<Tcp::Peer>& peer);

};


} // namespace Tcp

} // namespace Net
