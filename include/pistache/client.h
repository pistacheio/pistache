/*
   Mathieu Stefani, 29 janvier 2016

   The Http client
*/

#pragma once

#include <pistache/async.h>
#include <pistache/os.h>
#include <pistache/http.h>
#include <pistache/timer_pool.h>
#include <pistache/reactor.h>
#include <pistache/view.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Pistache {
namespace Http {

class ConnectionPool;
class Transport;

struct Connection : public std::enable_shared_from_this<Connection> {

    friend class ConnectionPool;

    using OnDone = std::function<void()>;

    Connection();

    struct RequestData {

        RequestData(
                Async::Resolver resolve, Async::Rejection reject,
                const Http::Request& request,
                std::chrono::milliseconds timeout,
                OnDone onDone)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
            , request(request)
            , timeout(timeout)
            , onDone(std::move(onDone))
        { }
        Async::Resolver resolve;
        Async::Rejection reject;

        Http::Request request;
        std::chrono::milliseconds timeout;
        OnDone onDone;
    };

    enum State : uint32_t {
        Idle,
        Used
    };

    enum ConnectionState {
        NotConnected,
        Connecting,
        Connected
    };

    void connect(const Address& addr);
    void close();
    bool isIdle() const;
    bool isConnected() const;
    bool hasTransport() const;
    void associateTransport(const std::shared_ptr<Transport>& transport);

    Async::Promise<Response> perform(
            const Http::Request& request,
            std::chrono::milliseconds timeout,
            OnDone onDone);

    Async::Promise<Response> asyncPerform(
            const Http::Request& request,
            std::chrono::milliseconds timeout,
            OnDone onDone);

    void performImpl(
            const Http::Request& request,
            std::chrono::milliseconds timeout,
            Async::Resolver resolve,
            Async::Rejection reject,
            OnDone onDone);

    Fd fd() const;
    void handleResponsePacket(const char* buffer, size_t totalBytes);
    void handleError(const char* error);
    void handleTimeout();

    std::string dump() const;

private:
    void processRequestQueue();

    struct RequestEntry {
        RequestEntry(
                Async::Resolver resolve, Async::Rejection reject,
                std::shared_ptr<TimerPool::Entry> timer,
                OnDone onDone)
          : resolve(std::move(resolve))
          , reject(std::move(reject))
          , timer(std::move(timer))
          , onDone(std::move(onDone))
        { }

        Async::Resolver resolve;
        Async::Rejection reject;
        std::shared_ptr<TimerPool::Entry> timer;
        OnDone onDone;
    };

    Fd fd_;

    struct sockaddr_in saddr;
    std::unique_ptr<RequestEntry> requestEntry;
    std::atomic<uint32_t> state_;
    std::atomic<ConnectionState> connectionState_;
    std::shared_ptr<Transport> transport_;
    Queue<RequestData> requestsQueue;

    TimerPool timerPool_;
    Private::Parser<Http::Response> parser;
};

class ConnectionPool {
public:
    ConnectionPool()
        : connsLock()
        , conns()
        , maxConnectionsPerHost()
    { }

    void init(size_t maxConnsPerHost);

    std::shared_ptr<Connection> pickConnection(const std::string& domain);
    static void releaseConnection(const std::shared_ptr<Connection>& connection);

    size_t usedConnections(const std::string& domain) const;
    size_t idleConnections(const std::string& domain) const;

    size_t availableConnections(const std::string& domain) const;

    void closeIdleConnections(const std::string& domain);

private:
    using Connections = std::vector<std::shared_ptr<Connection>>;
    using Lock = std::mutex;
    using Guard = std::lock_guard<Lock>;

    mutable Lock connsLock;
    std::unordered_map<std::string, Connections> conns;
    size_t maxConnectionsPerHost;
};

class Transport : public Aio::Handler {
public:

    PROTOTYPE_OF(Aio::Handler, Transport)

    Transport()
      : requestsQueue()
      , connectionsQueue()
      , connections()
      , timeouts()
      , timeoutsLock()
    { }

    Transport(const Transport &)
      : requestsQueue()
      , connectionsQueue()
      , connections()
      , timeouts()
      , timeoutsLock()
    { }

    void onReady(const Aio::FdSet& fds) override;
    void registerPoller(Polling::Epoll& poller) override;

    Async::Promise<void>
    asyncConnect(std::shared_ptr<Connection> connection, const struct sockaddr* address, socklen_t addr_len);

    Async::Promise<ssize_t> asyncSendRequest(
            std::shared_ptr<Connection> connection,
            std::shared_ptr<TimerPool::Entry> timer,
            std::string buffer);

private:

    enum WriteStatus {
        FirstTry,
        Retry
    };

    struct ConnectionEntry {
        ConnectionEntry(
                Async::Resolver resolve, Async::Rejection reject,
                std::shared_ptr<Connection> connection, const struct sockaddr* _addr, socklen_t _addr_len)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
            , connection(connection)
            , addr_len(_addr_len)
        {
            memcpy(&addr, _addr, addr_len);
        }

        const sockaddr *getAddr() const { return reinterpret_cast<const sockaddr *>(&addr); }

        Async::Resolver resolve;
        Async::Rejection reject;
        std::weak_ptr<Connection> connection;
        sockaddr_storage addr;
        socklen_t addr_len;
    };

    struct RequestEntry {
        RequestEntry(
                Async::Resolver resolve, Async::Rejection reject,
                std::shared_ptr<Connection> connection,
                std::shared_ptr<TimerPool::Entry> timer,
                std::string buf)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
            , connection(connection)
            , timer(timer)
            , buffer(std::move(buf))
        {
        }

        Async::Resolver resolve;
        Async::Rejection reject;
        std::weak_ptr<Connection> connection;
        std::shared_ptr<TimerPool::Entry> timer;
        std::string buffer;
    };


    PollableQueue<RequestEntry> requestsQueue;
    PollableQueue<ConnectionEntry> connectionsQueue;

    std::unordered_map<Fd, ConnectionEntry> connections;

    std::unordered_map<Fd, std::weak_ptr<Connection>> timeouts;
    using Lock = std::mutex;
    using Guard = std::lock_guard<Lock>;
    Lock timeoutsLock;

    void asyncSendRequestImpl(const RequestEntry& req, WriteStatus status = FirstTry);

    void handleRequestsQueue();
    void handleConnectionQueue();
    void handleReadableEntry(const Aio::FdSet::Entry& entry);
    void handleWritableEntry(const Aio::FdSet::Entry& entry);
    void handleHangupEntry(const Aio::FdSet::Entry& entry);
    void handleIncoming(std::shared_ptr<Connection> connection);
    void handleResponsePacket(const std::shared_ptr<Connection>& connection, const char* buffer, size_t totalBytes);
    void handleTimeout(const std::shared_ptr<Connection>& connection);

};


namespace Default {
    constexpr int Threads = 1;
    constexpr int MaxConnectionsPerHost = 8;
    constexpr bool KeepAlive = true;
}

class Client;

class RequestBuilder {
public:
    friend class Client;

    RequestBuilder& method(Method method);
    RequestBuilder& resource(const std::string& val);
    RequestBuilder& params(const Uri::Query& query);
    RequestBuilder& header(const std::shared_ptr<Header::Header>& header);

    template<typename H, typename... Args>
    typename
    std::enable_if<
        Header::IsHeader<H>::value, RequestBuilder&
    >::type
    header(Args&& ...args) {
        return header(std::make_shared<H>(std::forward<Args>(args)...));
    }

    RequestBuilder& cookie(const Cookie& cookie);
    RequestBuilder& body(const std::string& val);
    RequestBuilder& body(std::string&& val);

    RequestBuilder& timeout(std::chrono::milliseconds val);

    Async::Promise<Response> send();

private:
    explicit RequestBuilder(Client* const client)
        : client_(client)
        , request_()
        , timeout_(std::chrono::milliseconds(0))
    { }

    Client* const client_;

    Request request_;
    std::chrono::milliseconds timeout_;
};


class Client {
public:

   friend class RequestBuilder;

   struct Options {
       friend class Client;

       Options()
           : threads_(Default::Threads)
           , maxConnectionsPerHost_(Default::MaxConnectionsPerHost)
           , keepAlive_(Default::KeepAlive)
       { }

       Options& threads(int val);
       Options& keepAlive(bool val);
       Options& maxConnectionsPerHost(int val);

   private:
       int threads_;
       int maxConnectionsPerHost_;
       bool keepAlive_;
   };

   Client();
   ~Client();

   static Options options();
   void init(const Options& options = Options());

   RequestBuilder get(const std::string& resource);
   RequestBuilder post(const std::string& resource);
   RequestBuilder put(const std::string& resource);
   RequestBuilder patch(const std::string& resource);
   RequestBuilder del(const std::string& resource);

   void shutdown();

private:
   std::shared_ptr<Aio::Reactor> reactor_;

   ConnectionPool pool;
   Aio::Reactor::Key transportKey;

   std::atomic<uint64_t> ioIndex;

   using Lock = std::mutex;
   using Guard = std::lock_guard<Lock>;

   Lock queuesLock;
   std::unordered_map<std::string, MPMCQueue<std::shared_ptr<Connection::RequestData>, 2048>> requestsQueues;
   bool stopProcessPequestsQueues;

   RequestBuilder prepareRequest(const std::string& resource, Http::Method method);

   Async::Promise<Response> doRequest(
           Http::Request request,
           std::chrono::milliseconds timeout);

   void processRequestQueue();

};

} // namespace Http
} // namespace Pistache
