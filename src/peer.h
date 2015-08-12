/* peer.h
   Mathieu Stefani, 12 August 2015
   
  A class representing a TCP Peer
*/

#pragma once

#include "net.h"
#include <string>
#include <iostream>

namespace Net {

class Peer {
public:
    Peer();
    Peer(const Address& addr, const std::string& hostname);

    Address address() const;

    std::string hostname() const;

private:
    Address addr;
    std::string host;
};

std::ostream& operator<<(std::ostream& os, const Peer& peer);

} // namespace Net
