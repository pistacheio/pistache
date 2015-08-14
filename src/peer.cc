/* peer.cc
   Mathieu Stefani, 12 August 2015
   
*/

#include "peer.h"
#include <iostream>
#include <stdexcept>

namespace Net {

namespace Tcp {

using namespace std;

Peer::Peer()
    : fd_(-1)
{ }

Peer::Peer(const Address& addr, const string& host)
    : addr(addr)
    , host(host)
    , fd_(-1)
{ }

Address
Peer::address() const {
    return addr;
}

string
Peer::hostname() const {
    return host;
}

void
Peer::associateFd(int fd) {
    fd_ = fd;
}

int
Peer::fd() const {
    if (fd_ == -1) {
        throw std::runtime_error("The peer has no associated fd");
    }

    return fd_;
}

std::ostream& operator<<(std::ostream& os, const Peer& peer) {
    const auto& addr = peer.address();
    os << "(" << addr.host() << ", " << addr.port() << ") [" << peer.hostname() << "]";
    return os;
}

} // namespace Tcp

} // namespace Net
