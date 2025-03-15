/*
 * SPDX-FileCopyrightText: 2016 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
   Mathieu Stefani, 29 janvier 2016

   The Http client
*/

#pragma once

#include <pistache/async.h>
#include <pistache/http.h>
#include <pistache/os.h>
#include <pistache/reactor.h>
#include <pistache/timer_pool.h>
#include <pistache/view.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Pistache::Http::Experimental
{
#ifdef PISTACHE_USE_SSL
    enum class SslVerification { // Note: Affects only HTTPS connections
        On                = 1,
        OnExceptLocalhost = 2,
        Off               = 3
    };
#endif // PISTACHE_USE_SSL

    namespace Default
    {
        constexpr int Threads               = 1;
        constexpr int MaxConnectionsPerHost = 8;
        constexpr bool KeepAlive            = true;
        constexpr size_t MaxResponseSize    = std::numeric_limits<uint32_t>::max();
#ifdef PISTACHE_USE_SSL
        constexpr SslVerification ClientSslVerification = SslVerification::OnExceptLocalhost;
#endif // PISTACHE_USE_SSL
    } // namespace Default

    class Transport;
#ifdef PISTACHE_USE_SSL
    class SslConnection;
#endif // PISTACHE_USE_SSL

    class FdOrSslConn
    {

    public:
        FdOrSslConn()
            : fd_(PS_FD_EMPTY)
        { }
        FdOrSslConn(Fd _fd)
            : fd_(_fd)
        { }
#ifdef PISTACHE_USE_SSL
        FdOrSslConn(std::shared_ptr<SslConnection> _sslConn)
            : fd_(PS_FD_EMPTY)
            , ssl_conn_(_sslConn)
        { }
        std::shared_ptr<SslConnection> getSslConn() { return (ssl_conn_); }
#endif // PISTACHE_USE_SSL
        Fd getFd() const;
        Fd getNonSslSocketFd() const { return (fd_); }

        void close(); // closes either fd_ or ssl_conn_

    private:
        Fd fd_;
#ifdef PISTACHE_USE_SSL
        std::shared_ptr<SslConnection> ssl_conn_;
#endif // PISTACHE_USE_SSL
    };

    struct Connection : public std::enable_shared_from_this<Connection>
    {
        using OnDone = std::function<void()>;

        explicit Connection(size_t maxResponseSize);

        struct RequestData
        {

            RequestData(Async::Resolver resolve, Async::Rejection reject,
                        const Http::Request& request, OnDone onDone)
                : resolve(std::move(resolve))
                , reject(std::move(reject))
                , request(request)
                , onDone(std::move(onDone))
            { }
            Async::Resolver resolve;
            Async::Rejection reject;

            Http::Request request;
            OnDone onDone;
        };

        enum State : uint32_t { Idle,
                                Used };

        enum ConnectionState { NotConnected,
                               Connecting,
                               Connected };

        void connect(Address::Scheme scheme,
#ifdef PISTACHE_USE_SSL
                     SslVerification sslVerification,
#endif // PISTACHE_USE_SSL
                     const std::string& domain,
                     const std::string* page);
        // connectSocket and connectSsl are private
        void close();
        void closeFromRemoteClosedConnection(); // handling mutex already locked
        bool isIdle() const;
        bool tryUse();
        void setAsIdle();
        bool isConnected() const;
        bool hasTransport() const;
        void associateTransport(const std::shared_ptr<Transport>& transport);

#ifdef PISTACHE_USE_SSL
        // If using HTTPS, application should set hostChainPemFile_
        static const std::string& getHostChainPemFile();
        static void setHostChainPemFile(const std::string& _hostCPFl); // call once
#endif // PISTACHE_USE_SSL

        Async::Promise<Response> perform(const Http::Request& request, OnDone onDone);

        Async::Promise<Response> asyncPerform(const Http::Request& request,
                                              OnDone onDone);

        void performImpl(const Http::Request& request, Async::Resolver resolve,
                         Async::Rejection reject, OnDone onDone);

        std::shared_ptr<FdOrSslConn> fdOrSslConn() const
        {
            return (fd_or_ssl_conn_);
        }

        // Fd fd() const;
#ifdef PISTACHE_USE_SSL
        bool isSsl() const
        {
            return ((fd_or_ssl_conn_) && (fd_or_ssl_conn_->getSslConn()));
        }
#endif // PISTACHE_USE_SSL
        Fd fdDirectOrFromSsl() const
        {
            if (!fd_or_ssl_conn_)
                return (PS_FD_EMPTY);
            return (fd_or_ssl_conn_->getFd());
        }

        void handleResponsePacket(const char* buffer, size_t totalBytes);
        void handleError(const char* error);
        void handleTimeout();

        std::string dump() const;

    private:
        void processRequestQueue();

        void connectSocket(const Address& addr);
#ifdef PISTACHE_USE_SSL
        void connectSsl(const Address& addr, const std::string& domain,
                        SslVerification sslVerification);
#endif // PISTACHE_USE_SSL

        struct RequestEntry
        {
            RequestEntry(Async::Resolver resolve, Async::Rejection reject,
                         std::shared_ptr<TimerPool::Entry> timer, OnDone onDone)
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

#ifdef PISTACHE_USE_SSL
        static std::mutex hostChainPemFileMutex_;
        static std::string hostChainPemFile_;
#endif // PISTACHE_USE_SSL

        std::shared_ptr<FdOrSslConn> fd_or_ssl_conn_;
        // Fd fd_;

        struct sockaddr_storage saddr;
        std::unique_ptr<RequestEntry> requestEntry;
        std::atomic<uint32_t> state_;
        std::atomic<ConnectionState> connectionState_;
        std::shared_ptr<Transport> transport_;
        Queue<RequestData> requestsQueue;

        TimerPool timerPool_;
        ResponseParser parser;
    };

    class ConnectionPool
    {
    public:
        ConnectionPool() = default;

        void init(size_t maxConnectionsPerHost, size_t maxResponseSize);

        std::shared_ptr<Connection> pickConnection(const std::string& domain);
        static void releaseConnection(const std::shared_ptr<Connection>& connection);

        size_t usedConnections(const std::string& domain) const;
        size_t idleConnections(const std::string& domain) const;

        size_t availableConnections(const std::string& domain) const;

        void closeIdleConnections(const std::string& domain);
        void shutdown();

    private:
        using Connections = std::vector<std::shared_ptr<Connection>>;
        using Lock        = std::mutex;
        using Guard       = std::lock_guard<Lock>;

        mutable Lock connsLock;
        std::unordered_map<std::string, Connections> conns;
        size_t maxConnectionsPerHost;
        size_t maxResponseSize;
    };

    class Client;
    class RequestBuilder;

    namespace RequestBuilderAddOns
    {
        std::size_t bodySize(RequestBuilder& rb);
    }

    class RequestBuilder
    {
    public:
        friend class Client;

        friend std::size_t RequestBuilderAddOns::bodySize(RequestBuilder& rb);

        RequestBuilder& method(Method method);
        RequestBuilder& resource(const std::string& val);
        RequestBuilder& params(const Uri::Query& query);
        RequestBuilder& header(const std::shared_ptr<Header::Header>& header);

        template <typename H, typename... Args>
        typename std::enable_if<Header::IsHeader<H>::value, RequestBuilder&>::type
        header(Args&&... args)
        {
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
        { }

        Client* const client_;

        Request request_;
    };

    class Client
    {
    public:
        friend class RequestBuilder;

        struct Options
        {
            friend class Client;

            Options()
                : threads_(Default::Threads)
                , maxConnectionsPerHost_(Default::MaxConnectionsPerHost)
                , keepAlive_(Default::KeepAlive)
                , maxResponseSize_(Default::MaxResponseSize)
#ifdef PISTACHE_USE_SSL
                , clientSslVerification_(Default::ClientSslVerification)
#endif // PISTACHE_USE_SSL
            { }

            Options& threads(int val);
            Options& keepAlive(bool val);
            Options& maxConnectionsPerHost(int val);
            Options& maxResponseSize(size_t val);
#ifdef PISTACHE_USE_SSL
            Options& clientSslVerification(SslVerification val);
#endif // PISTACHE_USE_SSL

        private:
            int threads_;
            int maxConnectionsPerHost_;
            bool keepAlive_;
            size_t maxResponseSize_;
#ifdef PISTACHE_USE_SSL
            SslVerification clientSslVerification_;
#endif // PISTACHE_USE_SSL
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
        using Lock  = std::mutex;
        using Guard = std::lock_guard<Lock>;

        std::shared_ptr<Aio::Reactor> reactor_;

        ConnectionPool pool;
        Aio::Reactor::Key transportKey;

#ifdef PISTACHE_USE_SSL
        SslVerification sslVerification;
#endif // PISTACHE_USE_SSL

        std::atomic<uint64_t> ioIndex;

        // Note: queuesLock is declared before requestsQueues. This means that
        // when Client destructor is called, since members are destroyed in
        // reverse order of their declaration, requestsQueues will be destroyed
        // before queuesLock. Since we use queuesLock and
        // stopProcessRequestQueues to determine if we're allowed to access the
        // queues, we want to make sure they still exist at the moment we have
        // to make that determination.
        // I'm not sure if this ordering is necessary, but it may provide some
        // protection if destructor tries (indirectly) to access requestsQueues
        Lock queuesLock;
        bool stopProcessRequestQueues;

        std::unordered_map<std::string,
                           MPMCQueue<std::shared_ptr<Connection::RequestData>, 2048>>
            requestsQueues;

    private:
        RequestBuilder prepareRequest(const std::string& resource,
                                      Http::Method method);

        Async::Promise<Response> doRequest(Http::Request request);

        void processRequestQueue();
    };

} // namespace Pistache::Http
