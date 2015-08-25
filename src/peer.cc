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
    , data_(nullptr)
{ }

Peer::Peer(const Address& addr)
    : addr(addr)
    , fd_(-1)
    , data_(nullptr)
{ }

Address
Peer::address() const {
    return addr;
}

string
Peer::hostname() const {
    return hostname_;
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

void
Peer::setData(std::shared_ptr<void> data) {
    data_ = std::move(data);
}

std::shared_ptr<void>
Peer::data() const {
    return data_;
}

std::ostream& operator<<(std::ostream& os, const Peer& peer) {
    const auto& addr = peer.address();
    os << "(" << addr.host() << ", " << addr.port() << ") [" << peer.hostname() << "]";
    return os;
}

} // namespace Tcp

} // namespace Net
