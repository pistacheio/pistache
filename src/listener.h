/* listener.h
   Mathieu Stefani, 12 August 2015
   
  A TCP Listener
*/

#pragma once

#include "net.h"
#include "mailbox.h"
#include "os.h"
#include <vector>
#include <memory>
#include <thread>
#include <unordered_map>

namespace Net {

namespace Tcp {

class Peer;
class Message;

class Handler;

class IoWorker {
public:
    Mailbox<Message> mailbox;

    IoWorker();
    ~IoWorker();

    void start(const std::shared_ptr<Handler> &handler);
private:
    int epoll_fd;
    std::unique_ptr<std::thread> thread;
    std::unordered_map<Fd, std::shared_ptr<Peer>> peers;
    std::shared_ptr<Handler> handler_;

    std::shared_ptr<Peer> getPeer(Fd fd) const;

    void handleIncoming(const std::shared_ptr<Peer>& peer);
    void handleNewPeer(const std::shared_ptr<Peer>& peer);
    void run();
};

class Listener {
public:
    friend class IoWorker;

    Listener();

    Listener(const Address& address);
    void init(size_t workers);
    void setHandler(const std::shared_ptr<Handler>& handler);

    bool bind();
    bool bind(const Address& adress);

    void run();

    Address address() const;

private: 
    Address addr_; 
    int listen_fd;
    std::vector<std::unique_ptr<IoWorker>> ioGroup;
    std::shared_ptr<Handler> handler_;

    void dispatchPeer(const std::shared_ptr<Peer>& peer);
};

class Handler {
public:
    Handler();
    ~Handler();

    virtual void onInput(const char *buffer, size_t len, Tcp::Peer& peer) = 0;
    virtual void onOutput() = 0;

    virtual void onConnection();
    virtual void onDisconnection();

};


} // namespace Tcp

} // namespace Net
