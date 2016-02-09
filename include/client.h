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

struct Connection {

    friend class ConnectionPool;

    typedef std::function<void()> OnDone;

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

        std::string host;
        std::chrono::milliseconds timeout;
        Http::Request request;
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

    Connection()
        : fd(-1)
        , connectionState_(NotConnected)
    {
        state_.store(static_cast<uint32_t>(State::Idle));
    }

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

private:
    void processRequestQueue();


    std::atomic<uint32_t> state_;
    ConnectionState connectionState_;
    std::shared_ptr<Transport> transport_;
    Queue<RequestData> requestsQueue;
    Net::TimerPool timerPool_;
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
    asyncConnect(Fd fd, const struct sockaddr* address, socklen_t addr_len);

    void asyncSendRequest(
            Fd fd,
            std::shared_ptr<TimerPool::Entry> timer,
            const Buffer& buffer,
            Async::Resolver resolve,
            Async::Rejection reject,
            OnResponseParsed onParsed);

private:

    enum WriteStatus {
        FirstTry,
        Retry
    };

    struct PendingConnection {
        PendingConnection(
                Async::Resolver resolve, Async::Rejection reject,
                Fd fd, const struct sockaddr* addr, socklen_t addr_len)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
            , fd(fd)
            , addr(addr)
            , addr_len(addr_len)
        { }

        Async::Resolver resolve;
        Async::Rejection reject;
        Fd fd;
        const struct sockaddr* addr;
        socklen_t addr_len;
    };

    struct InflightRequest {
        InflightRequest(
                Async::Resolver resolve, Async::Rejection reject,
                Fd fd,
                std::shared_ptr<TimerPool::Entry> timer,
                const Buffer& buffer,
                OnResponseParsed onParsed = nullptr)
            : resolve_(std::move(resolve))
            , reject(std::move(reject))
            , fd(fd)
            , timer(std::move(timer))
            , buffer(buffer)
            , onParsed(onParsed)
        {
        }

        void feed(const char* buffer, size_t totalBytes) {
            if (!parser)
                parser.reset(new Private::Parser<Http::Response>());

            parser->feed(buffer, totalBytes);
        }

        void resolve(Http::Response response) {
            if (onParsed)
                onParsed();
            resolve_(std::move(response));
        }

        Async::Resolver resolve_;
        Async::Rejection reject;
        Fd fd;
        std::shared_ptr<TimerPool::Entry> timer;
        Buffer buffer;

        OnResponseParsed onParsed;

        std::shared_ptr<Private::Parser<Http::Response>> parser;
    };


    PollableQueue<InflightRequest> requestsQueue;
    PollableQueue<PendingConnection> connectionsQueue;

    std::unordered_map<Fd, PendingConnection> pendingConnections;
    std::unordered_map<Fd, std::deque<InflightRequest>> inflightRequests;
    std::unordered_map<Fd, Fd> timeouts;

    void asyncSendRequestImpl(InflightRequest& req, WriteStatus status = FirstTry);

    void handleRequestsQueue();
    void handleConnectionQueue();
    void handleIncoming(Fd fd);
    void handleResponsePacket(Fd fd, const char* buffer, size_t totalBytes);
    void handleTimeout(Fd fd);

};


namespace Default {
    constexpr int Threads = 1;
    constexpr int MaxConnections = 8;
    constexpr bool KeepAlive = true;
}

class Client {
public:

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

   RequestBuilder request(std::string resource);

   Async::Promise<Response> get(
           const Http::Request& request,
           std::chrono::milliseconds timeout = std::chrono::milliseconds(0));
   Async::Promise<Response> post(
           const Http::Request& req,
           std::chrono::milliseconds timeout = std::chrono::milliseconds(0));
   Async::Promise<Response> put(
           const Http::Request& req,
           std::chrono::milliseconds timeout = std::chrono::milliseconds(0));
   Async::Promise<Response> del(
           const Http::Request& req,
           std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

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

   Async::Promise<Response> doRequest(
           const Http::Request& req,
           Http::Method method,
           std::chrono::milliseconds timeout);

   void processRequestQueue();

};

} // namespace Experimental

} // namespace Http

} // namespace Net

