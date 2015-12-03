/* tcp.h
   Mathieu Stefani, 05 novembre 2015
   
   TCP
*/

#pragma once

#include "flags.h"
#include <memory>

namespace Net {

namespace Tcp {

class Peer;

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

class IoWorker;

class Handler {
public:
    friend class IoWorker;

    Handler();
    ~Handler();

    virtual void onInput(const char *buffer, size_t len, const std::shared_ptr<Tcp::Peer>& peer) = 0;

    virtual void onConnection(const std::shared_ptr<Tcp::Peer>& peer);
    virtual void onDisconnection(const std::shared_ptr<Tcp::Peer>& peer);

protected:
    IoWorker *io() { return io_; }

private:
    IoWorker *io_;
};

} // namespace Tcp

} // namespace Net
