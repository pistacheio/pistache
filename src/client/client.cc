/*
 * SPDX-FileCopyrightText: 2016 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
   Mathieu Stefani, 29 janvier 2016

   Implementation of the Http client
*/

#include <pistache/client.h>
#include <pistache/common.h>
#include <pistache/eventmeth.h>
#include <pistache/http.h>
#include <pistache/net.h>
#include <pistache/stream.h>

#include <netdb.h>

#ifdef _USE_LIBEVENT_LIKE_APPLE
// For sendfile(...) function
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#else
#include <sys/sendfile.h>
#endif

#include <sys/socket.h>
#include <sys/types.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>

namespace Pistache::Http::Experimental
{
    using NotifyOn = Polling::NotifyOn;

    static constexpr const char* UA = "pistache/0.1";

    namespace
    {
        // Using const_cast can result in undefined behavior.
        // C++17 provides a non-const .data() overload,
        // but url must be passed as a non-const reference (or by value)

        // if https_out is non null, then *https_out is set to true if URL
        // starts with "https://" and false otherwise
        std::pair<std::string_view, std::string_view> splitUrl(
            const std::string& url, bool* https_out = NULL)
        {
            RawStreamBuf<char> buf(const_cast<char*>(url.data()), url.size());
            StreamCursor cursor(&buf);

            if (https_out)
                *https_out = false;

            if (!match_string("http://", cursor))
            {
                bool looks_like_https = match_string("https://", cursor);
                if (https_out)
                    *https_out = looks_like_https;
            }

            match_string("www", cursor);
            match_literal('.', cursor);

            StreamCursor::Token hostToken(cursor);
            match_until({ '?', '/' }, cursor);

            std::string_view host(hostToken.rawText(), hostToken.size());
            std::string_view page(cursor.offset(), buf.endptr() - buf.curptr());

            return std::make_pair(host, page);
        }
    } // namespace

    namespace
    {
        template <typename H, typename... Args>
        void writeHeader(std::stringstream& streamBuf, Args&&... args)
        {
            using Http::crlf;

            H header(std::forward<Args>(args)...);

            streamBuf << H::Name << ": ";
            header.write(streamBuf);

            streamBuf << crlf;
        }

        void writeHeaders(std::stringstream& streamBuf,
                          const Http::Header::Collection& headers)
        {
            using Http::crlf;

            for (const auto& header : headers.list())
            {
                streamBuf << header->name() << ": ";
                header->write(streamBuf);
                streamBuf << crlf;
            }
        }

        void writeCookies(std::stringstream& streamBuf,
                          const Http::CookieJar& cookies)
        {
            using Http::crlf;

            streamBuf << "Cookie: ";
            bool first = true;
            for (const auto& cookie : cookies)
            {
                if (!first)
                {
                    streamBuf << "; ";
                }
                else
                {
                    first = false;
                }
                streamBuf << cookie.name << "=" << cookie.value;
            }

            streamBuf << crlf;
        }

        void writeRequest(std::stringstream& streamBuf, const Http::Request& request)
        {
            using Http::crlf;

            const auto& res         = request.resource();
            const auto [host, path] = splitUrl(res);
            const auto& body        = request.body();
            const auto& query       = request.query();

            auto pathStr = std::string(path);

            streamBuf << request.method() << " ";
            if (pathStr[0] != '/')
                streamBuf << '/';

            streamBuf << pathStr << query.as_str();
            streamBuf << " HTTP/1.1" << crlf;

            writeCookies(streamBuf, request.cookies());
            writeHeaders(streamBuf, request.headers());

            writeHeader<Http::Header::UserAgent>(streamBuf, UA);
            writeHeader<Http::Header::Host>(streamBuf, std::string(host));
            if (!body.empty())
            {
                writeHeader<Http::Header::ContentLength>(streamBuf, body.size());
            }
            streamBuf << crlf;

            if (!body.empty())
            {
                streamBuf << body;
            }
        }
    } // namespace

    class Transport : public Aio::Handler
    {
    public:
        PROTOTYPE_OF(Aio::Handler, Transport)

        Transport()
            : stopHandling(false) {};
        Transport(const Transport&)
            : requestsQueue()
            , connectionsQueue()
            , connections()
            , timeouts()
            , timeoutsLock()
            , stopHandling(false)
        { }

        void onReady(const Aio::FdSet& fds) override;
        void registerPoller(Polling::Epoll& poller) override;
        void unregisterPoller(Polling::Epoll& poller) override;

        Async::Promise<void> asyncConnect(std::shared_ptr<Connection> connection,
                                          const struct sockaddr* address,
                                          socklen_t addr_len);

        Async::Promise<ssize_t>
        asyncSendRequest(std::shared_ptr<Connection> connection,
                         std::shared_ptr<TimerPool::Entry> timer, std::string buffer);

#ifdef _USE_LIBEVENT
        std::shared_ptr<EventMethEpollEquiv> getEventMethEpollEquiv()
        {
            return (epoll_fd);
        }
#endif

        std::mutex& getHandlingMutex() { return (handlingMutex); }
        void setStopHandlingwMutexAlreadyLocked() { stopHandling = true; }

    private:
        enum WriteStatus { FirstTry,
                           Retry };

        struct ConnectionEntry
        {
            ConnectionEntry(Async::Resolver resolve, Async::Rejection reject,
                            std::shared_ptr<Connection> connection,
                            const struct sockaddr* _addr, socklen_t _addr_len)
                : resolve(std::move(resolve))
                , reject(std::move(reject))
                , connection(connection)
                , addr_len(_addr_len)
            {
                memcpy(&addr, _addr, addr_len);
            }

            const sockaddr* getAddr() const
            {
                return reinterpret_cast<const sockaddr*>(&addr);
            }

            Async::Resolver resolve;
            Async::Rejection reject;
            std::weak_ptr<Connection> connection;
            sockaddr_storage addr;
            socklen_t addr_len;
        };

        struct RequestEntry
        {
            RequestEntry(Async::Resolver resolve, Async::Rejection reject,
                         std::shared_ptr<Connection> connection,
                         std::shared_ptr<TimerPool::Entry> timer, std::string buf)
                : resolve(std::move(resolve))
                , reject(std::move(reject))
                , connection(connection)
                , timer(timer)
                , buffer(std::move(buf))
            { }

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

        using Lock  = std::mutex;
        using Guard = std::lock_guard<Lock>;
        Lock timeoutsLock;

        std::mutex handlingMutex;
        bool stopHandling;

#ifdef _USE_LIBEVENT
        std::shared_ptr<EventMethEpollEquiv> epoll_fd;
#endif

    private:
        void asyncSendRequestImpl(const RequestEntry& req,
                                  WriteStatus status = FirstTry);

        void handleRequestsQueue();
        void handleConnectionQueue();
        void handleReadableEntry(const Aio::FdSet::Entry& entry);
        void handleWritableEntry(const Aio::FdSet::Entry& entry);
        void handleHangupEntry(const Aio::FdSet::Entry& entry);
        void handleIncoming(std::shared_ptr<Connection> connection);
    };

    void Transport::onReady(const Aio::FdSet& fds)
    {
        PS_TIMEDBG_START_THIS;

        PS_LOG_DEBUG_ARGS("Locking handlingMutex %p", &handlingMutex);
        Guard guard(handlingMutex);
        if (stopHandling)
        {
            PS_LOG_DEBUG_ARGS("Ignoring ready fds for Transport %p "
                              "due to closed Fds",
                              this);
            PS_LOG_DEBUG_ARGS("Unlocking handlingMutex %p", &handlingMutex);
            return;
        }

        for (const auto& entry : fds)
        {
            if (entry.getTag() == connectionsQueue.tag())
            {
                handleConnectionQueue();
            }
            else if (entry.getTag() == requestsQueue.tag())
            {
                handleRequestsQueue();
            }
            else if (entry.isReadable())
            {
                handleReadableEntry(entry);
            }
            else if (entry.isWritable())
            {
                handleWritableEntry(entry);
            }
            else if (entry.isHangup())
            {
                handleHangupEntry(entry);
            }
            else
            {
                assert(false && "Unexpected event in entry");
            }
        }
        PS_LOG_DEBUG_ARGS("Unlocking handlingMutex %p", &handlingMutex);
    }

    void Transport::registerPoller(Polling::Epoll& poller)
    {
        PS_TIMEDBG_START_THIS;

        requestsQueue.bind(poller);
        connectionsQueue.bind(poller);

#ifdef _USE_LIBEVENT
        epoll_fd = poller.getEventMethEpollEquiv();
#endif
    }

    void Transport::unregisterPoller(Polling::Epoll& poller)
    {
#ifdef _USE_LIBEVENT
        epoll_fd = NULL;
#endif

        connectionsQueue.unbind(poller);
        requestsQueue.unbind(poller);
    }

    Async::Promise<void>
    Transport::asyncConnect(std::shared_ptr<Connection> connection,
                            const struct sockaddr* address, socklen_t addr_len)
    {
        PS_TIMEDBG_START_THIS;

        return Async::Promise<void>(
            [=](Async::Resolver& resolve, Async::Rejection& reject) {
                PS_TIMEDBG_START;

                ConnectionEntry entry(std::move(resolve), std::move(reject), connection,
                                      address, addr_len);
                connectionsQueue.push(std::move(entry));
            });
    }

    Async::Promise<ssize_t>
    Transport::asyncSendRequest(std::shared_ptr<Connection> connection,
                                std::shared_ptr<TimerPool::Entry> timer,
                                std::string buffer)
    {
        PS_TIMEDBG_START_THIS;

        return Async::Promise<ssize_t>(
            [&](Async::Resolver& resolve, Async::Rejection& reject) {
                PS_TIMEDBG_START;
                auto ctx = context();
                RequestEntry req(std::move(resolve), std::move(reject), connection,
                                 timer, std::move(buffer));
                if (std::this_thread::get_id() != ctx.thread())
                {
                    requestsQueue.push(std::move(req));
                }
                else
                {
                    asyncSendRequestImpl(req);
                }
            });
    }

    void Transport::asyncSendRequestImpl(const RequestEntry& req,
                                         WriteStatus status)
    {
        PS_TIMEDBG_START_THIS;

        const auto& buffer = req.buffer;
        auto conn          = req.connection.lock();
        if (!conn)
            throw std::runtime_error("Send request error");

        auto fd = conn->fd();
        if (fd == PS_FD_EMPTY)
        {
            PS_LOG_DEBUG_ARGS("Connection %p has empty fd", conn.get());

            conn->handleError("Could not send request");
            return;
        }

        ssize_t totalWritten = 0;
        for (;;)
        {
            const char* data           = buffer.data() + totalWritten;
            const ssize_t len          = buffer.size() - totalWritten;
            const ssize_t bytesWritten = ::send(GET_ACTUAL_FD(fd), data,
                                                len, 0);
            if (bytesWritten < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    if (status == FirstTry)
                    {
                        throw std::runtime_error("Unimplemented, fix me!");
                    }
                    reactor()->modifyFd(key(), fd, NotifyOn::Write, Polling::Mode::Edge);
                }
                else
                {
                    conn->handleError("Could not send request");
                }
                break;
            }
            else
            {
                totalWritten += bytesWritten;
                if (totalWritten == len)
                {
                    if (req.timer)
                    {
                        Guard guard(timeoutsLock);
                        timeouts.insert(std::make_pair(req.timer->fd(), conn));
                        req.timer->registerReactor(key(), reactor());
                    }
                    req.resolve(totalWritten);
                    break;
                }
            }
        }
    }

    void Transport::handleRequestsQueue()
    {
        PS_TIMEDBG_START_THIS;

        // Let's drain the queue
        for (;;)
        {
            auto req = requestsQueue.popSafe();
            if (!req)
                break;

            asyncSendRequestImpl(*req);
        }
    }

    void Transport::handleConnectionQueue()
    {
        PS_TIMEDBG_START_THIS;

        for (;;)
        {
            auto data = connectionsQueue.popSafe();
            if (!data)
                break;

            auto conn = data->connection.lock();
            if (!conn)
            {
                data->reject(Error::system("Failed to connect"));
                continue;
            }

            Fd fd = conn->fd();
            if (fd == PS_FD_EMPTY)
            {
                PS_LOG_DEBUG_ARGS("Connection %p has empty fd", conn.get());
                data->reject(Error::system("Failed to connect, fd now empty"));
                continue;
            }

            PS_LOG_DEBUG_ARGS("Calling ::connect fs %d", GET_ACTUAL_FD(fd));

            int res = ::connect(GET_ACTUAL_FD(fd), data->getAddr(), data->addr_len);
            PS_LOG_DEBUG_ARGS("::connect res %d, errno on fail %d (%s)",
                              res, (res < 0) ? errno : 0,
                              (res < 0) ? strerror(errno) : "success");

            if ((res == 0) || ((res == -1) && (errno == EINPROGRESS)))
            {
                reactor()->registerFdOneShot(key(), fd,
                                             NotifyOn::Write | NotifyOn::Hangup | NotifyOn::Shutdown);
            }
            else
            {
                data->reject(Error::system("Failed to connect"));
                continue;
            }
            connections.insert(std::make_pair(fd, std::move(*data)));
        }
    }

    void Transport::handleReadableEntry(const Aio::FdSet::Entry& entry)
    {
        PS_TIMEDBG_START_THIS;

        assert(entry.isReadable() && "Entry must be readable");

        auto tag = entry.getTag();
        const auto fd =
#ifdef _USE_LIBEVENT
            (tag.value());
#else
            static_cast<Fd>(tag.value());
#endif

        // Note: We only use the second element of *connIt (which is
        // "connection"); fd is the first element (the map key). Since *fd is
        // not in fact changed, it is OK to cast away the const of Fd here.
        Fd fd_for_find = ((Fd)fd);

        auto connIt = connections.find(fd_for_find);
        if (connIt != std::end(connections))
        {
            auto connection = connIt->second.connection.lock();
            if (connection)
            {
                handleIncoming(connection);
            }
            else
            {
                throw std::runtime_error(
                    "Connection error: problem with reading data from server");
            }
        }
        else
        {
            Guard guard(timeoutsLock);
            auto timerIt = timeouts.find(fd_for_find);
            if (timerIt != std::end(timeouts))
            {
                auto connection = timerIt->second.lock();
                if (connection)
                {
                    connection->handleTimeout();
                    timeouts.erase(fd_for_find);
                }
            }
        }
    }

    void Transport::handleWritableEntry(const Aio::FdSet::Entry& entry)
    {
        PS_TIMEDBG_START_THIS;

        assert(entry.isWritable() && "Entry must be writable");

        auto tag            = entry.getTag();
        const auto fd_const = static_cast<FdConst>(tag.value());

        // Note: We only use the second element of *connIt (which is
        // "connection"); fd is the first element (the map key). Since *fd is
        // not in fact changed, it is OK to cast away the const of Fd here.
        Fd fd = ((Fd)fd_const);

        auto connIt = connections.find(fd);
        if (connIt != std::end(connections))
        {
            auto& connectionEntry = connIt->second;
            auto connection       = connIt->second.connection.lock();
            if (connection)
            {
                Fd conn_fd = connection->fd();
                if (conn_fd == PS_FD_EMPTY)
                {
                    PS_LOG_DEBUG_ARGS("Connection %p has empty fd",
                                      connection.get());
                    connectionEntry.reject(Error::system("Connection lost"));
                }
                else
                {
                    connectionEntry.resolve();
                    // We are connected, we can start reading data now
                    reactor()->modifyFd(key(), conn_fd, NotifyOn::Read);
                }
            }
            else
            {
                connectionEntry.reject(Error::system("Connection lost"));
            }
        }
        else
        {
            throw std::runtime_error("Unknown fd");
        }
    }

    void Transport::handleHangupEntry(const Aio::FdSet::Entry& entry)
    {
        PS_TIMEDBG_START_THIS;

        assert(entry.isHangup() && "Entry must be hangup");

        auto tag = entry.getTag();

        const auto fd_const = static_cast<FdConst>(tag.value());
        // Note: We only use the second element of *connIt (which is
        // "connection"); fd is the first element (the map key). Since *fd is
        // not in fact changed, it is OK to cast away the const of Fd here.
        Fd fd = ((Fd)fd_const);

        auto connIt = connections.find(fd);
        if (connIt != std::end(connections))
        {
            auto& connectionEntry = connIt->second;
            connectionEntry.reject(Error::system("Could not connect"));
        }
        else
        {
            throw std::runtime_error("Unknown fd");
        }
    }

    void Transport::handleIncoming(std::shared_ptr<Connection> connection)
    {
        PS_TIMEDBG_START_THIS;

        ssize_t totalBytes = 0;

        for (;;)
        {
            char buffer[Const::MaxBuffer] = {
                0,
            };

            Fd conn_fd = connection->fd();
            if (conn_fd == PS_FD_EMPTY)
                break; // can happen if fd was closed meanwhile

            const ssize_t bytes = recv(GET_ACTUAL_FD(conn_fd),
                                       buffer, Const::MaxBuffer, 0);
            if (bytes == -1)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    connection->handleError(strerror(errno));
                }
                break;
            }
            else if (bytes == 0)
            {
                if (totalBytes == 0)
                {
                    connection->handleError("Remote closed connection");
                }
                connections.erase(conn_fd);
                connection->close();
                break;
            }
            else
            {
                totalBytes += bytes;
                connection->handleResponsePacket(buffer, bytes);
            }
        }
    }

    Connection::Connection(size_t maxResponseSize)
        : fd_(PS_FD_EMPTY)
        , requestEntry(nullptr)
        , parser(maxResponseSize)
    {
        state_.store(static_cast<uint32_t>(State::Idle));
        connectionState_.store(NotConnected);
    }

    void Connection::connect(const Address& addr)
    {
        PS_TIMEDBG_START_THIS;

        struct addrinfo hints = {};
        hints.ai_family       = addr.family();
        hints.ai_socktype     = SOCK_STREAM; /* Stream socket */

        const auto& host = addr.host();
        const auto& port = addr.port().toString();

        AddrInfo addressInfo;

        TRY(addressInfo.invoke(host.c_str(), port.c_str(), &hints));
        const addrinfo* addrs = addressInfo.get_info_ptr();

        int sfd = -1;

        for (const addrinfo* addr = addrs; addr; addr = addr->ai_next)
        {
            sfd = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
            PS_LOG_DEBUG_ARGS("::socket actual_fd %d", sfd);
            if (sfd < 0)
                continue;

            make_non_blocking(sfd);

            connectionState_.store(Connecting);

#ifdef _USE_LIBEVENT
            // We're openning a connection to a remote resource - I guess it
            // makes sense to allow either read or write?
            fd_ = TRY_NULL_RET(
                EventMethFns::em_event_new(
                    sfd, // pre-allocated file desc
                    EVM_READ | EVM_WRITE | EVM_PERSIST,
                    F_SETFDL_NOTHING, // setfd
                    O_NONBLOCK // setfl
                    ));
#else
            fd_ = sfd;
#endif

            transport_
                ->asyncConnect(shared_from_this(), addr->ai_addr, addr->ai_addrlen)
                .then(
                    [=]() {
                        socklen_t len = sizeof(saddr);
                        getsockname(sfd, reinterpret_cast<struct sockaddr*>(&saddr), &len);
                        connectionState_.store(Connected);
                        processRequestQueue();
                    },
                    PrintException());
            break;
        }

        if (sfd < 0)
            throw std::runtime_error("Failed to connect");
    }

    std::string Connection::dump() const
    {
        std::ostringstream oss;
        oss << "Connection(fd = " << fd_ << ", src_port = ";
        if (saddr.ss_family == AF_INET)
        {
            oss << ntohs(reinterpret_cast<const struct sockaddr_in*>(&saddr)->sin_port);
        }
        else if (saddr.ss_family == AF_INET6)
        {
            oss << ntohs(reinterpret_cast<const struct sockaddr_in6*>(&saddr)->sin6_port);
        }
        else
        {
            unreachable();
        }
        oss << ")";
        return oss.str();
    }

    bool Connection::isIdle() const
    {
        return static_cast<Connection::State>(state_.load()) == Connection::State::Idle;
    }

    bool Connection::tryUse()
    {
        auto curState = static_cast<uint32_t>(Connection::State::Idle);
        auto newState = static_cast<uint32_t>(Connection::State::Used);
        return state_.compare_exchange_strong(curState, newState);
    }

    void Connection::setAsIdle()
    {
        state_.store(static_cast<uint32_t>(Connection::State::Idle));
    }

    bool Connection::isConnected() const
    {
        return connectionState_.load() == Connected;
    }

    void Connection::close()
    {
        PS_TIMEDBG_START_THIS;

        if (transport_)
        {
            // we need to make sure that, if the connection's Fd has an event
            // pending, that Fd is not accessed by Transport::handleIncoming
            // (called from Transport::onReady after epoll returns) after the
            // Fd has been closed
            std::mutex& handling_mutex(
                transport_->getHandlingMutex());
            PS_LOG_DEBUG_ARGS("Locking handling_mutex %p",
                              &handling_mutex);
            std::lock_guard<std::mutex> guard(handling_mutex);

            transport_->setStopHandlingwMutexAlreadyLocked();

            connectionState_.store(NotConnected);
            CLOSE_FD(fd_);

            PS_LOG_DEBUG_ARGS("Unlocking handling_mutex %p",
                              &handling_mutex);
        }
        else
        {
            PS_LOG_DEBUG_ARGS("Closing connection %p without transport", this);

            connectionState_.store(NotConnected);
            CLOSE_FD(fd_);
        }
    }

    void Connection::associateTransport(
        const std::shared_ptr<Transport>& transport)
    {
        PS_TIMEDBG_START_THIS;

        if (transport_)
            throw std::runtime_error(
                "A transport has already been associated to the connection");

        transport_ = transport;
    }

    bool Connection::hasTransport() const { return transport_ != nullptr; }

    Fd Connection::fd() const
    {
#ifdef DEBUG
        if (fd_ == PS_FD_EMPTY) // can happen if fd was closed meanwhile
            PS_LOG_DEBUG_ARGS("Connection %p has empty fd", this);
#endif

        return fd_;
    }

    void Connection::handleResponsePacket(const char* buffer, size_t totalBytes)
    {
        PS_TIMEDBG_START_THIS;

        try
        {
            const bool result = parser.feed(buffer, totalBytes);
            if (!result)
            {
                handleError("Client: Too long packet");
                return;
            }
            if (parser.parse() == Private::State::Done)
            {
                if (requestEntry)
                {
                    if (requestEntry->timer)
                    {
                        requestEntry->timer->disarm();
                        timerPool_.releaseTimer(requestEntry->timer);
                    }

                    requestEntry->resolve(std::move(parser.response));
                    parser.reset();

                    auto onDone = requestEntry->onDone;

                    requestEntry.reset(nullptr);

                    if (onDone)
                        onDone();
                }
            }
        }
        catch (const std::exception& ex)
        {
            handleError(ex.what());
        }
    }

    void Connection::handleError(const char* error)
    {
        PS_TIMEDBG_START_THIS;

        if (requestEntry)
        {
            if (requestEntry->timer)
            {
                requestEntry->timer->disarm();
                timerPool_.releaseTimer(requestEntry->timer);
            }

            auto onDone = requestEntry->onDone;

            requestEntry->reject(Error(error));

            requestEntry.reset(nullptr);

            if (onDone)
                onDone();
        }
    }

    void Connection::handleTimeout()
    {
        PS_TIMEDBG_START_THIS;

        if (requestEntry)
        {
            requestEntry->timer->disarm();
            timerPool_.releaseTimer(requestEntry->timer);

            auto onDone = requestEntry->onDone;

            /* @API: create a TimeoutException */
            requestEntry->reject(std::runtime_error("Timeout"));

            requestEntry.reset(nullptr);

            if (onDone)
                onDone();
        }
    }

    Async::Promise<Response> Connection::perform(const Http::Request& request,
                                                 Connection::OnDone onDone)
    {
        PS_TIMEDBG_START_THIS;

        return Async::Promise<Response>(
            [=](Async::Resolver& resolve, Async::Rejection& reject) {
                PS_TIMEDBG_START;
                performImpl(request, std::move(resolve), std::move(reject),
                            std::move(onDone));
            });
    }

    Async::Promise<Response> Connection::asyncPerform(const Http::Request& request,
                                                      Connection::OnDone onDone)
    {
        PS_TIMEDBG_START_THIS;

        return Async::Promise<Response>(
            [=](Async::Resolver& resolve, Async::Rejection& reject) {
                PS_TIMEDBG_START;

                requestsQueue.push(RequestData(std::move(resolve), std::move(reject),
                                               request, std::move(onDone)));
            });
    }

    void Connection::performImpl(const Http::Request& request,
                                 Async::Resolver resolve, Async::Rejection reject,
                                 Connection::OnDone onDone)
    {
        PS_TIMEDBG_START_THIS;

        std::stringstream streamBuf;
        writeRequest(streamBuf, request);
        if (!streamBuf)
            reject(std::runtime_error("Could not write request"));
        std::string buffer = streamBuf.str();

        std::shared_ptr<TimerPool::Entry> timer(nullptr);
        auto timeout = request.timeout();
        if (timeout.count() > 0)
        {
            timer = timerPool_.pickTimer();
            timer->arm(timeout);
        }

        requestEntry = std::make_unique<RequestEntry>(std::move(resolve), std::move(reject),
                                                      timer, std::move(onDone));
        transport_->asyncSendRequest(shared_from_this(), timer, std::move(buffer));
    }

    void Connection::processRequestQueue()
    {
        PS_TIMEDBG_START_THIS;

        for (;;)
        {
            auto req = requestsQueue.popSafe();
            if (!req)
                break;

            performImpl(req->request, std::move(req->resolve), std::move(req->reject),
                        std::move(req->onDone));
        }
    }

    void ConnectionPool::init(size_t maxConnectionsPerHost,
                              size_t maxResponseSize)
    {
        this->maxConnectionsPerHost = maxConnectionsPerHost;
        this->maxResponseSize       = maxResponseSize;
    }

    std::shared_ptr<Connection>
    ConnectionPool::pickConnection(const std::string& domain)
    {
        PS_TIMEDBG_START_THIS;

        Connections pool;

        {
            Guard guard(connsLock);
            auto poolIt = conns.find(domain);
            if (poolIt == std::end(conns))
            {
                Connections connections;
                for (size_t i = 0; i < maxConnectionsPerHost; ++i)
                {
                    connections.push_back(std::make_shared<Connection>(maxResponseSize));
                }

                poolIt = conns.insert(std::make_pair(domain, std::move(connections))).first;
            }
            pool = poolIt->second;
        }

        for (auto& conn : pool)
        {
            if (conn->tryUse())
            {
                return conn;
            }
        }

        return nullptr;
    }

    void ConnectionPool::releaseConnection(
        const std::shared_ptr<Connection>& connection)
    {
        PS_TIMEDBG_START_ARGS("connection %p", connection.get());

        connection->setAsIdle();
    }

    size_t ConnectionPool::usedConnections(const std::string& domain) const
    {
        Connections pool;
        {
            Guard guard(connsLock);
            auto it = conns.find(domain);
            if (it == std::end(conns))
            {
                return 0;
            }
            pool = it->second;
        }

        return std::count_if(pool.begin(), pool.end(),
                             [](const std::shared_ptr<Connection>& conn) {
                                 return conn->isConnected();
                             });
    }

    size_t ConnectionPool::idleConnections(const std::string& domain) const
    {
        Connections pool;
        {
            Guard guard(connsLock);
            auto it = conns.find(domain);
            if (it == std::end(conns))
            {
                return 0;
            }
            pool = it->second;
        }

        return std::count_if(
            pool.begin(), pool.end(),
            [](const std::shared_ptr<Connection>& conn) { return conn->isIdle(); });
    }

    size_t ConnectionPool::availableConnections(const std::string& /*domain*/) const
    {
        return 0;
    }

    void ConnectionPool::closeIdleConnections(const std::string& /*domain*/)
    {
    }

    void ConnectionPool::shutdown()
    {
        PS_TIMEDBG_START_THIS;

        // close all connections
        Guard guard(connsLock);
        for (auto& it : conns)
        {
            for (auto& conn : it.second)
            {
                if (conn->isConnected())
                {
                    conn->close();
                }
            }
        }
    }

    namespace RequestBuilderAddOns
    {
        std::size_t bodySize(RequestBuilder& rb)
        {
            return (rb.request_.body().size());
        }
    }

    RequestBuilder& RequestBuilder::method(Method method)
    {
        request_.method_ = method;
        return *this;
    }

    RequestBuilder& RequestBuilder::resource(const std::string& val)
    {
        request_.resource_ = val;
        return *this;
    }

    RequestBuilder& RequestBuilder::params(const Uri::Query& query)
    {
        request_.query_ = query;
        return *this;
    }

    RequestBuilder&
    RequestBuilder::header(const std::shared_ptr<Header::Header>& header)
    {
        request_.headers_.add(header);
        return *this;
    }

    RequestBuilder& RequestBuilder::cookie(const Cookie& cookie)
    {
        request_.cookies_.add(cookie);
        return *this;
    }

    RequestBuilder& RequestBuilder::body(const std::string& val)
    {
        request_.body_ = val;
        return *this;
    }

    RequestBuilder& RequestBuilder::body(std::string&& val)
    {
        request_.body_ = std::move(val);
        return *this;
    }

    RequestBuilder& RequestBuilder::timeout(std::chrono::milliseconds val)
    {
        request_.timeout_ = val;
        return *this;
    }

    Async::Promise<Response> RequestBuilder::send()
    {
        PS_TIMEDBG_START_THIS;

        return client_->doRequest(request_);
    }

    Client::Options& Client::Options::threads(int val)
    {
        threads_ = val;
        return *this;
    }

    Client::Options& Client::Options::keepAlive(bool val)
    {
        keepAlive_ = val;
        return *this;
    }

    Client::Options& Client::Options::maxConnectionsPerHost(int val)
    {
        maxConnectionsPerHost_ = val;
        return *this;
    }

    Client::Options& Client::Options::maxResponseSize(size_t val)
    {
        maxResponseSize_ = val;
        return *this;
    }

    Client::Client()
        : reactor_(Aio::Reactor::create())
        , pool()
        , transportKey()
        , ioIndex(0)
        , queuesLock()
        , stopProcessRequestQueues(false)
        , requestsQueues()
    { }

    Client::~Client()
    {
        PS_TIMEDBG_START_THIS;

        Guard guard(queuesLock);
        assert(stopProcessRequestQueues == true && "You must explicitly call shutdown method of Client object");
    }

    Client::Options Client::options() { return Client::Options(); }

    void Client::init(const Client::Options& options)
    {
        pool.init(options.maxConnectionsPerHost_, options.maxResponseSize_);
        reactor_->init(Aio::AsyncContext(options.threads_));
        transportKey = reactor_->addHandler(std::make_shared<Transport>());
        reactor_->run();
    }

    void Client::shutdown()
    {
        PS_TIMEDBG_START_THIS;

        reactor_->shutdown();

        { // encapsulate
            GUARD_AND_DBG_LOG(queuesLock);
            stopProcessRequestQueues = true;

            // Note: Do not hold queuesLock locked beyond here - otherwise you
            // can get into a deadlock with a transport_'s handling_mutex. Here
            // we are locking queuesLock and then during shutdown we'll lock
            // the handling_mutex. Conversely in onReady (handling), we'll
            // lock handling_mutex first and may subsequently lock queuesLock
            // to allow us to change a queue. By doing the locking in opposite
            // order, without unlocking queuesLock here thanks to this
            // encapsulate, we'd create a race-condition/deadlock.
        }

        // Note about the shutdown procedure. pool.shutdown()
        // (ConnectionPool::shutdown()) calls Connection::close() for each
        // connection in ConnectionPool::conns. Connection::close() claims and
        // holds the transport_'s handling_mutex before excuting the connection
        // and Fd close.
        //
        // Meanwhile, Transport::onReady claims and holds the handling_mutex
        // while executing. So a connection close (which includes an Fd close)
        // cannot happen while handling is going on - i.e. the Fd cannot be
        // closed just when the handling needs it (which might otherwise happen
        // when Transport::onReady called handleReadableEntry which in turn
        // called handleIncoming(...)).
        //
        // If the close() gets possession of the handling_mutex first, that is
        // also managed - the close will remove the Fd that is being closed
        // from the set of ready Fds before releasing the mutex and allowing
        // onReady to proceed.

        pool.shutdown();

        PS_LOG_DEBUG_ARGS("Unlocking queuesLock %p", &queuesLock);
    }

    RequestBuilder Client::get(const std::string& resource)
    {
        PS_TIMEDBG_START_THIS;
        return prepareRequest(resource, Http::Method::Get);
    }

    RequestBuilder Client::post(const std::string& resource)
    {
        PS_TIMEDBG_START_THIS;
        return prepareRequest(resource, Http::Method::Post);
    }

    RequestBuilder Client::put(const std::string& resource)
    {
        PS_TIMEDBG_START_THIS;
        return prepareRequest(resource, Http::Method::Put);
    }

    RequestBuilder Client::patch(const std::string& resource)
    {
        PS_TIMEDBG_START_THIS;
        return prepareRequest(resource, Http::Method::Patch);
    }

    RequestBuilder Client::del(const std::string& resource)
    {
        PS_TIMEDBG_START_THIS;
        return prepareRequest(resource, Http::Method::Delete);
    }

    RequestBuilder Client::prepareRequest(const std::string& resource,
                                          Http::Method method)
    {
        PS_TIMEDBG_START_THIS;
        RequestBuilder builder(this);
        builder.resource(resource).method(method);

        return builder;
    }

    Async::Promise<Response> Client::doRequest(Http::Request request)
    {
        PS_TIMEDBG_START_THIS;

        // request.headers_.add<Header::Connection>(ConnectionControl::KeepAlive);
        request.headers().remove<Header::UserAgent>();
        auto resourceData = request.resource();

        PS_LOG_DEBUG_ARGS("resourceData %s", resourceData.c_str());

        bool https_url = false;
        auto resource  = splitUrl(resourceData, &https_url);
        if (https_url)
        {
            PS_LOG_WARNING_ARGS("URL %s is https, but Client class does not "
                                "currently support HTTPS",
                                resourceData.c_str());
        }

        auto conn = pool.pickConnection(std::string(resource.first));

        if (conn == nullptr)
        {
            PS_LOG_DEBUG("No connection found");

            return Async::Promise<Response>([this, resource = std::move(resource),
                                             request](Async::Resolver& resolve,
                                                      Async::Rejection& reject) {
                PS_TIMEDBG_START;

                PS_LOG_DEBUG_ARGS("Locking queuesLock %p", &queuesLock);
                Guard guard(queuesLock);

                auto data = std::make_shared<Connection::RequestData>(
                    std::move(resolve), std::move(reject), std::move(request), nullptr);
                auto& queue = requestsQueues[std::string(resource.first)];
                if (!queue.enqueue(data))
                    data->reject(std::runtime_error("Queue is full"));

                PS_LOG_DEBUG_ARGS("Unlocking queuesLock %p", &queuesLock);
            });
        }
        else
        {
            PS_LOG_DEBUG_ARGS("Connection found %p", conn.get());
            if (!conn->hasTransport())
            {
                PS_LOG_DEBUG("No transport yet on connection");

                auto transports = reactor_->handlers(transportKey);
                auto index      = ioIndex.fetch_add(1) % transports.size();

                auto transport = std::static_pointer_cast<Transport>(transports[index]);
                PS_LOG_DEBUG_ARGS("Associating transport %p on connection %p",
                                  transport.get(), conn.get());
                conn->associateTransport(transport);
            }

            if (!conn->isConnected())
            {
                PS_LOG_DEBUG_ARGS("Connection %p not connected yet",
                                  conn.get());

                std::weak_ptr<Connection> weakConn = conn;
                auto res                           = conn->asyncPerform(request, [this, weakConn]() {
                    auto conn = weakConn.lock();
                    if (conn)
                    {
                        pool.releaseConnection(conn);
                        processRequestQueue();
                    }
                });

                PS_LOG_DEBUG("Making addr");
                Address addr(helpers::httpAddr(resource.first,
                                               https_url ? 443 : 0 /* default port*/));

                PS_LOG_DEBUG_ARGS("Connection %p calling connect", conn.get());
                conn->connect(addr);
                return res;
            }

            std::weak_ptr<Connection> weakConn = conn;
            return conn->perform(request, [this, weakConn]() {
                auto conn = weakConn.lock();
                if (conn)
                {
                    PS_LOG_DEBUG("Release connection");
                    pool.releaseConnection(conn);
                    processRequestQueue();
                }
                PS_LOG_DEBUG("Request performed");
            });
        }
    }

    void Client::processRequestQueue()
    {
        PS_TIMEDBG_START_THIS;

        if (stopProcessRequestQueues)
        {
            PS_LOG_DEBUG("Already shutting down, skip processRequestQueue");
            return;
        }

        PS_LOG_DEBUG_ARGS("Locking queuesLock %p", &queuesLock);
        Guard guard(queuesLock);

        if (stopProcessRequestQueues)
        {
            PS_LOG_DEBUG("Already shutting down, skip processRequestQueue");
            PS_LOG_DEBUG_ARGS("Unlocking queuesLock %p", &queuesLock);
            return;
        }

        for (auto& queues : requestsQueues)
        {
            for (;;)
            {
                const auto& domain = queues.first;
                auto conn          = pool.pickConnection(domain);
                if (!conn)
                    break;

                auto& queue = queues.second;
                std::shared_ptr<Connection::RequestData> data;
                if (!queue.dequeue(data))
                {
                    pool.releaseConnection(conn);
                    break;
                }

                conn->performImpl(data->request, std::move(data->resolve),
                                  std::move(data->reject), [this, conn]() {
                                      pool.releaseConnection(conn);
                                      processRequestQueue();
                                  });
            }
        }

        PS_LOG_DEBUG_ARGS("Unlocking queuesLock %p", &queuesLock);
    }

} // namespace Pistache::Http
