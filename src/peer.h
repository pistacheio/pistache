/* peer.h
   Mathieu Stefani, 12 August 2015
   
  A class representing a TCP Peer
*/

#pragma once

#include "net.h"
#include "os.h"
#include <string>
#include <iostream>

namespace Net {

namespace Tcp {

class Peer {
public:
    Peer();
    Peer(const Address& addr);

    Address address() const;
    std::string hostname() const;

    void associateFd(Fd fd);
    Fd fd() const;

private:
    Address addr;
    std::string hostname_;
    Fd fd_;
};

std::ostream& operator<<(std::ostream& os, const Peer& peer);

} // namespace Tcp

} // namespace Net
