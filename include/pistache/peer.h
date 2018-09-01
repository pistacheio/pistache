/* peer.h
   Mathieu Stefani, 12 August 2015
   
  A class representing a TCP Peer
*/

#pragma once

#include <string>
#include <iostream>
#include <memory>
#include <unordered_map>

#include <pistache/net.h>
#include <pistache/os.h>
#include <pistache/async.h>
#include <pistache/stream.h>

namespace Pistache {
namespace Tcp {

class Transport;

class Peer {
public:
    friend class Transport;

    Peer();
    Peer(const Address& addr);

    Address address() const;
    std::string hostname() const;

    void associateFd(Fd fd);
    Fd fd() const;

    void putData(std::string name, std::shared_ptr<void> data);

    std::shared_ptr<void> getData(std::string name) const;
    template<typename T>
    std::shared_ptr<T> getData(std::string name) const {
        return std::static_pointer_cast<T>(getData(std::move(name)));
    }

    std::shared_ptr<void> tryGetData(std::string name) const;
    template<typename T>
    std::shared_ptr<T> tryGetData(std::string name) const {
        auto data = tryGetData(std::move(name));
        if (data == nullptr) return nullptr;

        return std::static_pointer_cast<T>(data);
    }

    Async::Promise<ssize_t> send(const Buffer& buffer, int flags = 0);

private:
    void associateTransport(Transport* transport);
    Transport* transport() const;

    Transport* transport_;

    Address addr;
    std::string hostname_;
    Fd fd_;
    std::unordered_map<std::string, std::shared_ptr<void>> data_;
};

std::ostream& operator<<(std::ostream& os, const Peer& peer);

} // namespace Tcp
} // namespace Pistache
