/* http.h
   Mathieu Stefani, 13 August 2015

   Http Layer
*/

#pragma once

#include <algorithm>
#include <array>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <sys/timerfd.h>

#include <pistache/async.h>
#include <pistache/cookie.h>
#include <pistache/http_defs.h>
#include <pistache/http_headers.h>
#include <pistache/mime.h>
#include <pistache/net.h>
#include <pistache/stream.h>
#include <pistache/tcp.h>
#include <pistache/transport.h>

namespace Pistache {
namespace Tcp {
class Peer;
}
namespace Http {

namespace details {
struct prototype_tag {};

template <typename P> struct IsHttpPrototype {
  template <typename U> static auto test(U *) -> decltype(typename U::tag());
  template <typename U> static auto test(...) -> std::false_type;

  static constexpr bool value =
      std::is_same<decltype(test<P>(nullptr)), prototype_tag>::value;
};
} // namespace details

#define HTTP_PROTOTYPE(Class)                                                  \
  PROTOTYPE_OF(Pistache::Tcp::Handler, Class)                                  \
  typedef Pistache::Http::details::prototype_tag tag;

namespace Private {
class RequestLineStep;
class ResponseLineStep;
class HeadersStep;
class BodyStep;
} // namespace Private

template <class CharT, class Traits>
std::basic_ostream<CharT, Traits> &crlf(std::basic_ostream<CharT, Traits> &os) {
  static constexpr char CRLF[] = {0xD, 0xA};
  os.write(CRLF, 2);

  return os;
}

// 4. HTTP Message
class Message {
public:
  friend class Private::HeadersStep;
  friend class Private::BodyStep;
  friend class ResponseWriter;

  Message() = default;
  explicit Message(Version version);

  Message(const Message &other) = default;
  Message &operator=(const Message &other) = default;

  Message(Message &&other) = default;
  Message &operator=(Message &&other) = default;

  Version version() const;
  Code code() const;

  std::string body() const;

  const CookieJar &cookies() const;
  CookieJar &cookies();
  const Header::Collection &headers() const;
  Header::Collection &headers();

protected:
  Version version_ = Version::Http11;
  Code code_;

  std::string body_;

  CookieJar cookies_;
  Header::Collection headers_;
};

namespace Uri {

class Query {
public:
  Query();
  explicit Query(
      std::initializer_list<std::pair<const std::string, std::string>> params);

  void add(std::string name, std::string value);
  Optional<std::string> get(const std::string &name) const;
  bool has(const std::string &name) const;
  // Return empty string or "?key1=value1&key2=value2" if query exist
  std::string as_str() const;

  void clear() { params.clear(); }

  // \brief Return iterator to the beginning of the parameters map
  std::unordered_map<std::string, std::string>::const_iterator
  parameters_begin() const {
    return params.begin();
  }

  // \brief Return iterator to the end of the parameters map
  std::unordered_map<std::string, std::string>::const_iterator
  parameters_end() const {
    return params.end();
  }

  // \brief returns all parameters given in the query
  std::vector<std::string> parameters() const {
    std::vector<std::string> keys;
    std::transform(
        params.begin(), params.end(), std::back_inserter(keys),
        [](const std::unordered_map<std::string, std::string>::value_type
               &pair) { return pair.first; });
    return keys;
  }

private:
  // first is key second is value
  std::unordered_map<std::string, std::string> params;
};
} // namespace Uri

// 5. Request
class Request : public Message {
public:
  friend class Private::RequestLineStep;

  friend class RequestBuilder;

  Request() = default;

  Request(const Request &other) = default;
  Request &operator=(const Request &other) = default;

  Request(Request &&other) = default;
  Request &operator=(Request &&other) = default;

  Method method() const;
  const std::string &resource() const;

  const Uri::Query &query() const;

/* @Investigate: this is disabled because of a lock in the shared_ptr /
   weak_ptr implementation of libstdc++. Under contention, we experience a
   performance drop of 5x with that lock

   If this turns out to be a problem, we might be able to replace the
   weak_ptr trick to detect peer disconnection by a plain old "observer"
   pointer to a tcp connection with a "stale" state
*/
#ifdef LIBSTDCPP_SMARTPTR_LOCK_FIXME
  std::shared_ptr<Tcp::Peer> peer() const;
#endif

  const Address &address() const;

  void copyAddress(const Address &address) { address_ = address; }

  std::chrono::milliseconds timeout() const;

private:
#ifdef LIBSTDCPP_SMARTPTR_LOCK_FIXME
  void associatePeer(const std::shared_ptr<Tcp::Peer> &peer) {
    if (peer_.use_count() > 0)
      throw std::runtime_error("A peer was already associated to the response");

    peer_ = peer;
  }
#endif

  Method method_;
  std::string resource_;
  Uri::Query query_;

#ifdef LIBSTDCPP_SMARTPTR_LOCK_FIXME
  std::weak_ptr<Tcp::Peer> peer_;
#endif
  Address address_;
  std::chrono::milliseconds timeout_ = std::chrono::milliseconds(0);
};

class Handler;
class ResponseWriter;

class Timeout {
public:
  friend class ResponseWriter;

  explicit Timeout(Timeout &&other)
      : handler(other.handler), request(std::move(other.request)),
        transport(other.transport), armed(other.armed), timerFd(other.timerFd),
        peer(std::move(other.peer)) {
    // cppcheck-suppress useInitializationList
    other.timerFd = -1;
  }

  Timeout &operator=(Timeout &&other) {
    handler = other.handler;
    transport = other.transport;
    request = std::move(other.request);
    armed = other.armed;
    timerFd = other.timerFd;
    other.timerFd = -1;
    peer = std::move(other.peer);
    return *this;
  }

  ~Timeout();

  template <typename Duration> void arm(Duration duration) {
    Async::Promise<uint64_t> p([=](Async::Deferred<uint64_t> deferred) {
      timerFd = TRY_RET(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));
      transport->armTimer(timerFd, duration, std::move(deferred));
    });

    p.then(
        [=](uint64_t numWakeup) {
          this->armed = false;
          this->onTimeout(numWakeup);
          close(timerFd);
        },
        [=](std::exception_ptr exc) { std::rethrow_exception(exc); });

    armed = true;
  }

  void disarm();

  bool isArmed() const;

private:
  Timeout(const Timeout &other) = default;

  Timeout(Tcp::Transport *transport_, Handler *handler_, Request request_,
          std::weak_ptr<Tcp::Peer> peer_);

  void onTimeout(uint64_t numWakeup);

  Handler *handler;
  Request request;
  Tcp::Transport *transport;
  bool armed;
  Fd timerFd;
  std::weak_ptr<Tcp::Peer> peer;
};

class ResponseStream final {
public:
  friend class ResponseWriter;

  ResponseStream(ResponseStream &&other);

  ResponseStream &operator=(ResponseStream &&other);

  template <typename T>
  friend ResponseStream &operator<<(ResponseStream &stream, const T &val);

  std::streamsize write(const char *data, std::streamsize sz);

  void flush();
  void ends();

private:
  ResponseStream(Message &&other, std::weak_ptr<Tcp::Peer> peer,
                 Tcp::Transport *transport, Timeout timeout, size_t streamSize,
                 size_t maxResponseSize);

  std::shared_ptr<Tcp::Peer> peer() const;

  Message response_;
  std::weak_ptr<Tcp::Peer> peer_;
  DynamicStreamBuf buf_;
  Tcp::Transport *transport_;
  Timeout timeout_;
};

inline ResponseStream &ends(ResponseStream &stream) {
  stream.ends();
  return stream;
}

inline ResponseStream &flush(ResponseStream &stream) {
  stream.flush();
  return stream;
}

template <typename T>
ResponseStream &operator<<(ResponseStream &stream, const T &val) {
  Size<T> size;

  std::ostream os(&stream.buf_);
  os << std::hex << size(val) << crlf;
  os << val << crlf;

  return stream;
}

inline ResponseStream &operator<<(ResponseStream &stream,
                                  ResponseStream &(*func)(ResponseStream &)) {
  return (*func)(stream);
}

// 6. Response
// @Investigate public inheritence
class Response : public Message {
public:
  friend class Private::ResponseLineStep;

  Response() = default;
  explicit Response(Version version);

  Response(const Response &other) = default;
  Response &operator=(const Response &other) = default;
  Response(Response &&other) = default;
  Response &operator=(Response &&other) = default;
};

class ResponseWriter final {
public:
  static constexpr size_t DefaultStreamSize = 512;

  friend Async::Promise<ssize_t>
  serveFile(ResponseWriter &, const std::string &, const Mime::MediaType &);

  friend class Handler;
  friend class Timeout;

  ResponseWriter &operator=(const ResponseWriter &other) = delete;

  friend class Private::ResponseLineStep;

  //
  // C++11: std::weak_ptr move constructor is C++14 only so the default
  // version of move constructor / assignement operator does not work and we
  // have to define it ourself
  ResponseWriter(ResponseWriter &&other);

  ResponseWriter &operator=(ResponseWriter &&other) = default;

  void setMime(const Mime::MediaType &mime);

  /* @Feature: add helper functions for common http return code:
   * - halt() -> 404
   * - movedPermantly -> 301
   * - moved() -> 302
   */
  Async::Promise<ssize_t>
  sendMethodNotAllowed(const std::vector<Http::Method> &supportedMethods);

  Async::Promise<ssize_t> send(Code code, const std::string &body = "",
                               const Mime::MediaType &mime = Mime::MediaType());

  template <size_t N>
  Async::Promise<ssize_t>
  send(Code code, const char (&arr)[N],
       const Mime::MediaType &mime = Mime::MediaType()) {
    return sendImpl(code, arr, N - 1, mime);
  }

  Async::Promise<ssize_t> send(Code code, const char *data, const size_t size,
                               const Mime::MediaType &mime = Mime::MediaType());

  ResponseStream stream(Code code, size_t streamSize = DefaultStreamSize);

  template <typename Duration> void timeoutAfter(Duration duration) {
    timeout_.arm(duration);
  }

  const CookieJar &cookies() const;
  CookieJar &cookies();

  const Header::Collection &headers() const;
  Header::Collection &headers();

  Timeout &timeout();

  std::shared_ptr<Tcp::Peer> peer() const;

  // Returns total count of HTTP bytes (headers, cookies, body) written when
  // sending the response.  Result valid AFTER ResponseWriter.send() is called.
  ssize_t getResponseSize() const { return sent_bytes_; }

  // Returns HTTP result code that was sent with the response.
  Code getResponseCode() const { return response_.code(); }

  // Unsafe API

  DynamicStreamBuf *rdbuf();

  DynamicStreamBuf *rdbuf(DynamicStreamBuf *other);

  ResponseWriter clone() const;

private:
  ResponseWriter(Tcp::Transport *transport, Request request, Handler *handler,
                 std::weak_ptr<Tcp::Peer> peer);

  ResponseWriter(const ResponseWriter &other);

  Async::Promise<ssize_t> sendImpl(Code code, const char *data,
                                   const size_t size,
                                   const Mime::MediaType &mime);

  Async::Promise<ssize_t> putOnWire(const char *data, size_t len);

  Response response_;
  std::weak_ptr<Tcp::Peer> peer_;
  DynamicStreamBuf buf_;
  Tcp::Transport *transport_;
  Timeout timeout_;
  ssize_t sent_bytes_;
};

Async::Promise<ssize_t>
serveFile(ResponseWriter &writer, const std::string &fileName,
          const Mime::MediaType &contentType = Mime::MediaType());

namespace Private {

enum class State { Again, Next, Done };

struct Step {
  explicit Step(Message *request);

  virtual ~Step() = default;

  virtual State apply(StreamCursor &cursor) = 0;

  static void raise(const char *msg, Code code = Code::Bad_Request);

protected:
  Message *message;
};

class RequestLineStep : public Step {
public:
  explicit RequestLineStep(Request *request) : Step(request) {}

  State apply(StreamCursor &cursor) override;
};

class ResponseLineStep : public Step {
public:
  explicit ResponseLineStep(Response *response) : Step(response) {}

  State apply(StreamCursor &cursor) override;
};

class HeadersStep : public Step {
public:
  explicit HeadersStep(Message *request) : Step(request) {}

  State apply(StreamCursor &cursor) override;
};

class BodyStep : public Step {
public:
  explicit BodyStep(Message *message_)
      : Step(message_), chunk(message_), bytesRead(0) {}

  State apply(StreamCursor &cursor) override;

private:
  struct Chunk {
    enum Result { Complete, Incomplete, Final };

    explicit Chunk(Message *message_)
        : message(message_), bytesRead(0), size(-1) {}

    Result parse(StreamCursor &cursor);

    void reset() {
      bytesRead = 0;
      size = -1;
    }

  private:
    Message *message;
    size_t bytesRead;
    ssize_t size;
  };

  State parseContentLength(StreamCursor &cursor,
                           const std::shared_ptr<Header::ContentLength> &cl);
  State
  parseTransferEncoding(StreamCursor &cursor,
                        const std::shared_ptr<Header::TransferEncoding> &te);

  Chunk chunk;
  size_t bytesRead;
};

class ParserBase {
public:
  explicit ParserBase(size_t maxDataSize);

  ParserBase(const ParserBase &) = delete;
  ParserBase &operator=(const ParserBase &) = delete;
  ParserBase(ParserBase &&) = default;
  ParserBase &operator=(ParserBase &&) = default;

  virtual ~ParserBase() = default;

  bool feed(const char *data, size_t len);
  virtual void reset();
  State parse();

protected:
  static constexpr size_t StepsCount = 3;

  std::array<std::unique_ptr<Step>, StepsCount> allSteps;
  size_t currentStep = 0;

private:
  ArrayStreamBuf<char> buffer;
  StreamCursor cursor;
};

template <typename Message> class ParserImpl;

template <> class ParserImpl<Http::Request> : public ParserBase {

public:
  explicit ParserImpl(size_t maxDataSize);

  void reset() override;

  Request request;
};

template <> class ParserImpl<Http::Response> : public ParserBase {
public:
  explicit ParserImpl(size_t maxDataSize);

  Response response;
};

} // namespace Private

using Parser = Private::ParserBase;
using RequestParser = Private::ParserImpl<Http::Request>;
using ResponseParser = Private::ParserImpl<Http::Response>;

class Handler : public Tcp::Handler {
public:
  virtual void onRequest(const Request &request, ResponseWriter response) = 0;

  virtual void onTimeout(const Request &request, ResponseWriter response);

  void setMaxRequestSize(size_t value);
  size_t getMaxRequestSize() const;
  void setMaxResponseSize(size_t value);
  size_t getMaxResponseSize() const;

  virtual ~Handler() override {}

private:
  void onConnection(const std::shared_ptr<Tcp::Peer> &peer) override;
  void onDisconnection(const std::shared_ptr<Tcp::Peer> &peer) override;
  void onInput(const char *buffer, size_t len,
               const std::shared_ptr<Tcp::Peer> &peer) override;
  RequestParser &getParser(const std::shared_ptr<Tcp::Peer> &peer) const;

private:
  size_t maxRequestSize_ = Const::DefaultMaxRequestSize;
  size_t maxResponseSize_ = Const::DefaultMaxResponseSize;
};

template <typename H, typename... Args>
std::shared_ptr<H> make_handler(Args &&... args) {
  static_assert(std::is_base_of<Handler, H>::value,
                "An http handler must inherit from the Http::Handler class");
  static_assert(details::IsHttpPrototype<H>::value,
                "An http handler must be an http prototype, did you forget the "
                "HTTP_PROTOTYPE macro ?");

  return std::make_shared<H>(std::forward<Args>(args)...);
}

} // namespace Http
} // namespace Pistache
