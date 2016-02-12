/* 
   Mathieu Stefani, 29 janvier 2016
   
   The Http client
*/

#pragma once
#include "async.h" 
#include "os.h"
#include "http.h"
#include "io.h"
#include "timer_pool.h"
#include <atomic>
#include <sys/types.h>
#include <sys/socket.h>
#include <deque>

namespace Net {

namespace Http {

namespace Experimental {

class ConnectionPool;
class Transport;

struct Connection : public std::enable_shared_from_this<Connection> {

    friend class ConnectionPool;

    typedef std::function<void()> OnDone;

    Connection()
        : fd(-1)
        , connectionState_(NotConnected)
        , inflightCount(0)
        , responsesReceived(0)
    {
        state_.store(static_cast<uint32_t>(State::Idle));
    }

    struct RequestData {

        RequestData(
                Async::Resolver resolve, Async::Rejection reject,
                const Http::Request& request,
                std::string host,
                std::chrono::milliseconds timeout,
                OnDone onDone)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
            , request(request)
            , host(std::move(host))
            , timeout(timeout)  
            , onDone(std::move(onDone))
        { }
        Async::Resolver resolve;
        Async::Rejection reject;

        Http::Request request;
        std::string host;
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

    void connect(Net::Address addr);
    void close();
    bool isConnected() const;
    bool hasTransport() const;
    void associateTransport(const std::shared_ptr<Transport>& transport);

    Async::Promise<Response> perform(
            const Http::Request& request,
            std::string host,
            std::chrono::milliseconds timeout,
            OnDone onDone);

    void performImpl(
            const Http::Request& request,
            std::string host,
            std::chrono::milliseconds timeout,
            Async::Resolver resolve,
            Async::Rejection reject,
            OnDone onDone);

    Fd fd;

    void handleResponsePacket(const char* buffer, size_t totalBytes);
    void handleTimeout();

    std::string dump() const;

private:
    std::atomic<int> inflightCount;
    std::atomic<int> responsesReceived;
    struct sockaddr_in saddr;


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

    std::atomic<uint32_t> state_;
    ConnectionState connectionState_;
    std::shared_ptr<Transport> transport_;
    Queue<RequestData> requestsQueue;

    std::deque<RequestEntry> inflightRequests;

    Net::TimerPool timerPool_;
    Private::Parser<Http::Response> parser_;
};

struct ConnectionPool {
    void init(size_t max = 1);

    std::shared_ptr<Connection> pickConnection();
    void releaseConnection(const std::shared_ptr<Connection>& connection);

    size_t availableCount() const;

private:
    std::atomic<uint32_t> usedCount;
    std::vector<std::shared_ptr<Connection>> connections;
};

class Transport : public Io::Handler {
public:

    PROTOTYPE_OF(Io::Handler, Transport)

    typedef std::function<void()> OnResponseParsed;

    void onReady(const Io::FdSet& fds);
    void registerPoller(Polling::Epoll& poller);

    Async::Promise<void>
    asyncConnect(const std::shared_ptr<Connection>& connection, const struct sockaddr* address, socklen_t addr_len);

    Async::Promise<ssize_t> asyncSendRequest(
            const std::shared_ptr<Connection>& connection,
            std::shared_ptr<TimerPool::Entry> timer,
            const Buffer& buffer);

private:

    enum WriteStatus {
        FirstTry,
        Retry
    };

    struct ConnectionEntry {
        ConnectionEntry(
                Async::Resolver resolve, Async::Rejection reject,
                std::shared_ptr<Connection> connection, const struct sockaddr* addr, socklen_t addr_len)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
            , connection(std::move(connection))
            , addr(addr)
            , addr_len(addr_len)
        { }

        Async::Resolver resolve;
        Async::Rejection reject;
        std::shared_ptr<Connection> connection;
        const struct sockaddr* addr;
        socklen_t addr_len;
    };

    struct RequestEntry {
        RequestEntry(
                Async::Resolver resolve, Async::Rejection reject,
                std::shared_ptr<Connection> connection,
                std::shared_ptr<TimerPool::Entry> timer,
                const Buffer& buffer)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
            , connection(std::move(connection))
            , timer(std::move(timer))
            , buffer(buffer)
        {
        }

        Async::Resolver resolve;
        Async::Rejection reject;
        std::shared_ptr<Connection> connection;
        std::shared_ptr<TimerPool::Entry> timer;
        Buffer buffer;

    };


    PollableQueue<RequestEntry> requestsQueue;
    PollableQueue<ConnectionEntry> connectionsQueue;

    std::unordered_map<Fd, ConnectionEntry> connections;
    std::unordered_map<Fd, RequestEntry> requests;
    std::unordered_map<Fd, std::shared_ptr<Connection>> timeouts;

    void asyncSendRequestImpl(const RequestEntry& req, WriteStatus status = FirstTry);

    void handleRequestsQueue();
    void handleConnectionQueue();
    void handleIncoming(const std::shared_ptr<Connection>& connection);
    void handleResponsePacket(const std::shared_ptr<Connection>& connection, const char* buffer, size_t totalBytes);
    void handleTimeout(const std::shared_ptr<Connection>& connection);

};


namespace Default {
    constexpr int Threads = 1;
    constexpr int MaxConnections = 8;
    constexpr bool KeepAlive = true;
}

class Client;

class RequestBuilder {
public:
    friend class Client;

    RequestBuilder& method(Method method);
    RequestBuilder& resource(std::string val);
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
    RequestBuilder& body(std::string val);

    RequestBuilder& timeout(std::chrono::milliseconds value);

    Async::Promise<Response> send();

private:
    RequestBuilder(Client* const client)
        : client_(client)
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
           , maxConnections_(Default::MaxConnections)
           , keepAlive_(Default::KeepAlive)

       { }

       Options& threads(int val);
       Options& maxConnections(int val);
       Options& keepAlive(bool val);
   private:
       int threads_;
       int maxConnections_;
       bool keepAlive_;
   };

   Client(const std::string &base);

   static Options options();
   void init(const Options& options);

   RequestBuilder get(std::string resource);
   RequestBuilder post(std::string resource);
   RequestBuilder put(std::string resource);
   RequestBuilder del(std::string resource);

   void shutdown();

private:

   Io::ServiceGroup io_;
   std::string url_;
   std::string host_;
   Net::Address addr_;

   ConnectionPool pool;
   std::shared_ptr<Transport> transport_;

   std::atomic<uint64_t> ioIndex;
   Queue<Connection::RequestData> requestsQueue;

   RequestBuilder prepareRequest(std::string resource, Http::Method method);

   Async::Promise<Response> doRequest(
           Http::Request req,
           std::chrono::milliseconds timeout);

   void processRequestQueue();

};

} // namespace Experimental

} // namespace Http

} // namespace Net

