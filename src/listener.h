/* listener.h
   Mathieu Stefani, 12 August 2015
   
  A TCP Listener
*/

#pragma once

#include "tcp.h"
#include "net.h"
#include "os.h"
#include "flags.h"
#include "io.h"
#include <vector>
#include <memory>

namespace Net {

namespace Tcp {

class Peer;
class Handler;

void setSocketOptions(Fd fd, Flags<Options> options);

class Listener {
public:

    struct Load {
        typedef std::chrono::system_clock::time_point TimePoint;
        double global;
        std::vector<double> workers;

        std::vector<rusage> raw;
        TimePoint tick;
    };

    Listener();
    ~Listener();

    Listener(const Address& address);
    void init(
            size_t workers, Flags<Options> options = Options::None,
            int backlog = Const::MaxBacklog);
    void setHandler(const std::shared_ptr<Handler>& handler);

    bool bind();
    bool bind(const Address& adress);

    bool isBound() const;

    void run();
    void runThreaded();

    void shutdown();

    Async::Promise<Load> requestLoad(const Load& old);

    Options options() const;
    Address address() const;

    void pinWorker(size_t worker, const CpuSet& set);

private: 
    Address addr_; 
    int listen_fd;
    int backlog_;
    NotifyFd shutdownFd;
    Polling::Epoll poller;

    std::vector<std::unique_ptr<IoWorker>> ioGroup;
    Flags<Options> options_;
    std::shared_ptr<Handler> handler_;
    std::unique_ptr<std::thread> acceptThread;

    void shutdownIo();
    void handleNewConnection();
    void dispatchPeer(const std::shared_ptr<Peer>& peer);

};

} // namespace Tcp

} // namespace Net
