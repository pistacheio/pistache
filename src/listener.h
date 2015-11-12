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

} // namespace Tcp

} // namespace Net
