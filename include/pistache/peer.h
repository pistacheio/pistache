/* peer.h
   Mathieu Stefani, 12 August 2015

  A class representing a TCP Peer
*/

#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

#include <pistache/async.h>
#include <pistache/http.h>
#include <pistache/net.h>
#include <pistache/os.h>
#include <pistache/stream.h>

#ifdef PISTACHE_USE_SSL

#include <openssl/ssl.h>

#endif /* PISTACHE_USE_SSL */

namespace Pistache {
namespace Tcp {

class Transport;

class Peer {
public:
  friend class Transport;

  ~Peer();

  static std::shared_ptr<Peer> Create(Fd fd, const Address &addr);
  static std::shared_ptr<Peer> CreateSSL(Fd fd, const Address &addr, void *ssl);

  const Address &address() const;
  const std::string &hostname();
  Fd fd() const;

  void *ssl() const;

  void putData(std::string name, std::shared_ptr<Http::Parser> data);
  std::shared_ptr<Http::Parser> getData(std::string name) const;
  std::shared_ptr<Http::Parser> tryGetData(std::string name) const;

  Async::Promise<ssize_t> send(const RawBuffer &buffer, int flags = 0);

protected:
  Peer(Fd fd, const Address &addr, void *ssl);

private:
  void associateTransport(Transport *transport);
  Transport *transport() const;

  Transport *transport_ = nullptr;
  Fd fd_ = -1;
  Address addr;

  std::string hostname_;
  std::unordered_map<std::string, std::shared_ptr<Http::Parser>> data_;

  void *ssl_ = nullptr;
};

std::ostream &operator<<(std::ostream &os, Peer &peer);

} // namespace Tcp
} // namespace Pistache
