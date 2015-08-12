/* peer.cc
   Mathieu Stefani, 12 August 2015
   
*/

#include "peer.h"
#include <iostream>

namespace Net {

using namespace std;

Peer::Peer()
{ }

Peer::Peer(const Address& addr, const string& host)
    : addr(addr)
    , host(host)
{ }

Address
Peer::address() const {
    return addr;
}

string
Peer::hostname() const {
    return host;
}

std::ostream& operator<<(std::ostream& os, const Peer& peer) {
    const auto& addr = peer.address();
    os << "(" << addr.host() << ", " << addr.port() << ") [" << peer.hostname() << "]";
    return os;
}

} // namespace Net
