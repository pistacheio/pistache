/* peer.h
   Mathieu Stefani, 12 August 2015
   
  A class representing a TCP Peer
*/

#pragma once

#include "net.h"
#include "os.h"
#include <string>
#include <iostream>
#include <memory>

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

    std::shared_ptr<void> data() const;
    void setData(std::shared_ptr<void> data);

private:

    Address addr;
    std::string hostname_;
    Fd fd_;
    std::shared_ptr<void> data_;
};

std::ostream& operator<<(std::ostream& os, const Peer& peer);

} // namespace Tcp

} // namespace Net
