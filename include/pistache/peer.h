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

#ifdef PISTACHE_USE_SSL

#include <openssl/ssl.h>


#endif /* PISTACHE_USE_SSL */




namespace Pistache {
    namespace Http { namespace Private { class ParserBase; } }
namespace Tcp {

class Transport;

class Peer {
public:
    friend class Transport;

    Peer();
    Peer(const Address& addr);
    ~Peer();
    
    const Address& address() const;
    const std::string& hostname() const;

    void associateFd(Fd fd);
    Fd fd() const;

    void associateSSL(void *ssl);
    void *ssl(void) const;

    void putData(std::string name, std::shared_ptr<Pistache::Http::Private::ParserBase> data);
    std::shared_ptr<Pistache::Http::Private::ParserBase> getData(std::string name) const;
    std::shared_ptr<Pistache::Http::Private::ParserBase> tryGetData(std::string name) const;

    Async::Promise<ssize_t> send(const RawBuffer& buffer, int flags = 0);

private:
    void associateTransport(Transport* transport);
    Transport* transport() const;

    Transport* transport_;
    Address addr;
    Fd fd_;

    std::string hostname_;
    std::unordered_map<std::string, std::shared_ptr<Pistache::Http::Private::ParserBase>> data_;

    void *ssl_;
};

std::ostream& operator<<(std::ostream& os, const Peer& peer);

} // namespace Tcp
} // namespace Pistache
