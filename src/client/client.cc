/*
 * SPDX-FileCopyrightText: 2016 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
   Mathieu Stefani, 29 janvier 2016

   Implementation of the Http client
*/

#include <pistache/winornix.h>

#include <pistache/client.h>
#include <pistache/common.h>
#include <pistache/eventmeth.h>
#include <pistache/http.h>
#include <pistache/net.h>
#ifdef PISTACHE_USE_SSL
#include <pistache/sslclient.h>
#endif // PISTACHE_USE_SSL
#include <pistache/stream.h>

#include PST_NETDB_HDR
#include PST_SOCKET_HDR
#include PIST_SOCKFNS_HDR

// ps_sendfile.h includes sys/uio.h in macOS, and sys/sendfile.h in Linux
#include <pistache/ps_sendfile.h>

#include PST_STRERROR_R_HDR

#include <sys/types.h>

#include <algorithm>
#include <cstring> // for std::memcpy
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
            const std::string& url, bool remove_subdomain, bool* https_out = nullptr)
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

            // Skipping the subdomain ("www.") is a good idea if we want to
            // resolve to an IP address for a TCP or SSL connection. It is a
            // bad idea if we are constructing the REST request - it may result
            // in a 301 HTTP error ("document moved" aka a redirection). E.g.
            // curl "https://google.com" - gets a 301 reponse, while curl
            // "https://www.google.com" - gets a 200 response i.e. success.
            //
            // ALSO - this is a weak way to remove the subdomain. There are
            // many other possible subdomains than just "www". To actually
            // identify the subdomain, you need to identify the TLD (".com" or
            // whatever), work back to the domain, and then isolate the
            // subdomain part of the URL. See https://publicsuffix.org and e.g.
            // https://stackoverflow.com/questions/288810/
            //                                     get-the-subdomain-from-a-url
            // TODO !!!!
            if (remove_subdomain)
            {
                match_string("www", cursor);
                match_literal('.', cursor);
            }

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

            bool is_https           = false;
            const auto& res         = request.resource();
            const auto [host, path] = splitUrl(res, false, &is_https);
            // For splitUrl, false => do not remove subdomain from host name
            const auto& body  = request.body();
            const auto& query = request.query();

            auto pathStr = std::string(path);

            streamBuf << request.method() << " ";
            if (pathStr[0] != '/')
                streamBuf << '/';

            streamBuf << pathStr << query.as_str();
            streamBuf << " HTTP/1.1" << crlf;

            writeCookies(streamBuf, request.cookies());
            writeHeaders(streamBuf, request.headers());

            std::string host_str( // add port if HTTPs and not already specified
                std::string(host) + std::string((is_https && (std::string(host).find(':') == std::string::npos)) ? ":443" : ""));

            if (!request.headers().has("User-Agent"))
            {
                writeHeader<Http::Header::UserAgent>(streamBuf, UA);
            }

            if (!request.headers().has("Host"))
            {
                writeHeader<Http::Header::Host>(streamBuf, host_str);
            }

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
            : stopHandling(false) { };
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
                                          PST_SOCKLEN_T addr_len);

        Async::Promise<PST_SSIZE_T>
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
                // Note: Sanitizer may object to invocation of memcpy with null
                // _addr, even if addr_len is zero
                if (_addr_len)
                {
                    // Note - the "cast" here avoids a warning of mismatched
                    // signedness in Windows
                    if (addr_len > (static_cast<socklen_t>(sizeof(addr))))
                    {
                        PS_LOG_ERR_ARGS("addr_len %d bigger than %d",
                                        addr_len, sizeof(addr));
                        throw std::invalid_argument("addr_len too big");
                    }

                    std::memcpy(&addr, _addr, addr_len);
                }
                else
                {
                    std::memset(&addr, 0, sizeof(addr));
                }
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
        epoll_fd = nullptr;
#endif

        connectionsQueue.unbind(poller);
        requestsQueue.unbind(poller);
    }

    Async::Promise<void>
    Transport::asyncConnect(std::shared_ptr<Connection> connection,
                            const struct sockaddr* address,
                            PST_SOCKLEN_T addr_len)
    {
        PS_TIMEDBG_START_THIS;

        return Async::Promise<void>(
            [connection, address, addr_len, this](Async::Resolver& resolve, Async::Rejection& reject) {
                PS_TIMEDBG_START;

                ConnectionEntry entry(std::move(resolve), std::move(reject), connection,
                                      address, addr_len);
                connectionsQueue.push(std::move(entry));
            });
    }

    Async::Promise<PST_SSIZE_T>
    Transport::asyncSendRequest(std::shared_ptr<Connection> connection,
                                std::shared_ptr<TimerPool::Entry> timer,
                                std::string buffer)
    {
        PS_TIMEDBG_START_THIS;

        return Async::Promise<PST_SSIZE_T>(
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

        Fd fd(conn->fdDirectOrFromSsl());
        if (fd == PS_FD_EMPTY)
        {
            PS_LOG_DEBUG_ARGS("Connection %p has empty fd", conn.get());

            conn->handleError("Could not send request");
            return;
        }
        // fd is either the direct fd of 'conn', or, in the ssl case, the fd of
        // the SslConnection

        PST_SSIZE_T totalWritten = 0;
        for (;;)
        {
            const char* data         = buffer.data() + totalWritten;
            const PST_SSIZE_T len    = buffer.size() - totalWritten;
            PST_SSIZE_T bytesWritten = -1;

#ifdef PISTACHE_USE_SSL
            if (conn->isSsl())
            {
// Set -DPST_SSL_REQ_DBG on build (or comment in below) to log send content
// Comment out PST_SSL_REQ_DBG to suppress log of send content
#ifdef DEBUG
// #define PST_SSL_REQ_DBG DEBUG // Comment in/out as desired
#endif
#ifdef PST_SSL_REQ_DBG
                PS_LOG_DEBUG_ARGS("SSL send: fd %" PIST_QUOTE(PS_FD_PRNTFCD) ", len %d, ptr %p, data: %s",
                                  fd, len, data, data);
#else
                PS_LOG_DEBUG_ARGS("SSL send: fd %" PIST_QUOTE(PS_FD_PRNTFCD) ", len %d, ptr %p",
                                  fd, len, data);
#endif
                bytesWritten = conn->fdOrSslConn()->getSslConn()->sslRawSend(data, len);
                PS_LOG_DEBUG_ARGS("SSL sent: res %d, fd %" PIST_QUOTE(PS_FD_PRNTFCD) ", data %p, len %d",
                                  bytesWritten, fd, data, len);
            }
            else
#endif // PISTACHE_USE_SSL
            {
                bytesWritten = PST_SOCK_SEND(GET_ACTUAL_FD(fd), data, len, 0);
            }

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
                else if (errno == ECONNREFUSED)
                {
                    PS_LOG_DEBUG("Could not send, connection refused");
                    conn->handleError("Could not send, connection refused");
                }
                else
                {
                    PST_DBG_DECL_SE_ERR_P_EXTRA;
                    PS_LOG_DEBUG_ARGS("Could not send request, errno %d %s",
                                      errno, PST_STRERROR_R_ERRNO);
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

            Fd fd = conn->fdDirectOrFromSsl();
            if (fd == PS_FD_EMPTY)
            {
                PS_LOG_DEBUG_ARGS("Connection %p has empty fd", conn.get());
                data->reject(Error::system("Failed to connect, fd now empty"));
                continue;
            }

#ifdef PISTACHE_USE_SSL
            if (conn->isSsl())
            {
                std::shared_ptr<SslConnection> ssl_conn(
                    conn->fdOrSslConn()->getSslConn());
                if (!ssl_conn)
                {
                    PST_DBG_DECL_SE_ERR_P_EXTRA;
                    PS_LOG_DEBUG_ARGS("getSslConn, errno on fail %d (%s)",
                                      errno, PST_STRERROR_R_ERRNO);

                    data->reject(Error::system(
                        "Failed to connect, null ssl_conn"));
                    return;
                }

                reactor()->registerFdOneShot(
                    key(), fd,
                    NotifyOn::Write | NotifyOn::Hangup | NotifyOn::Shutdown);

                // connected synchronously
                PS_LOG_DEBUG("Resolving SSL connection");
                data->resolve();

                // We are connected, we can start reading data now
                reactor()->modifyFd(key(), fd, NotifyOn::Read);
            }
            else
#endif // PISTACHE_USE_SSL
            {
                PS_LOG_DEBUG_ARGS("Calling ::connect fs %d",
                                  GET_ACTUAL_FD(fd));

                int res = PST_SOCK_CONNECT(GET_ACTUAL_FD(fd),
                                           data->getAddr(), data->addr_len);
                PST_DBG_DECL_SE_ERR_P_EXTRA;
                PS_LOG_DEBUG_ARGS("::connect res %d, errno on fail %d (%s)",
                                  res, (res < 0) ? errno : 0,
                                  (res < 0) ? PST_STRERROR_R_ERRNO : "success");

                if ((res == 0) || ((res == -1) && (errno == EINPROGRESS))
#ifdef _IS_WINDOWS
                    || ((res == -1) && (errno == EWOULDBLOCK))
// In Linux, EWOULDBLOCK can be set by ::connect, but only
// for Unix domain sockets (i.e. sockets being used for
// inter-process communication) which is not our situation
//
// In Windows, EWOULDBLOCK is typically set here for
// non-blocking sockets
#endif
                )
                {
                    reactor()->registerFdOneShot(key(), fd,
                                                 NotifyOn::Write | NotifyOn::Hangup | NotifyOn::Shutdown);
                }
                else
                {
                    data->reject(Error::system("Failed to connect"));
                    continue;
                }
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

        PS_LOG_DEBUG_ARGS("Readable entry fd %" PIST_QUOTE(PS_FD_PRNTFCD), fd);

        // Note: We only use the second element of *connIt (which is
        // "connection"); fd is the first element (the map key). Since *fd is
        // not in fact changed, it is OK to cast away the const of Fd here.
        Fd fd_for_find = PS_CAST_AWAY_CONST_FD(fd);

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
        Fd fd = PS_CAST_AWAY_CONST_FD(fd_const);

        auto connIt = connections.find(fd);
        if (connIt != std::end(connections))
        {
            auto& connectionEntry = connIt->second;
            auto connection       = connIt->second.connection.lock();
            if (connection)
            {
                auto conn_fd = connection->fdDirectOrFromSsl();
                if (conn_fd == PS_FD_EMPTY)
                {
                    PS_LOG_DEBUG_ARGS("Connection %p has empty fd",
                                      connection.get());

                    connectionEntry.reject("Connection has empty fd");
                    return;
                    // conn_fd is either the direct fd of 'connection', or, in
                    // the ssl case, the fd of the SslConnection
                }

#ifdef PISTACHE_USE_SSL
                if (connection->isSsl())
                { // Complete SSL verification
                    try
                    {
                        std::shared_ptr<SslConnection> ssl_conn(
                            connection->fdOrSslConn()->getSslConn());
                        if (!ssl_conn)
                            throw std::runtime_error("Null ssl_conn");
                    }
                    catch (...)
                    {
                        connectionEntry.reject(Error::system(
                            "SSL failure, could not connect"));
                        throw std::runtime_error(
                            "SSL failure, could not connect");
                    }
                }
#endif // PISTACHE_USE_SSL

                connectionEntry.resolve();
                // We are connected, we can start reading data now
                reactor()->modifyFd(key(), conn_fd, NotifyOn::Read);
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
        Fd fd = PS_CAST_AWAY_CONST_FD(fd_const);

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

        PST_SSIZE_T totalBytes                   = 0;
        unsigned int max_buffer                  = Const::MaxBuffer;
        const unsigned int max_max_buffer        = 8 * 1024 * 1024;
        char stack_buffer[Const::MaxBuffer + 16] = {
            0,
        };
        char* buffer = &(stack_buffer[0]);
        std::unique_ptr<char[]> buffer_uptr;

#ifdef PISTACHE_USE_SSL
        bool know_readable = true; // true only in first pass of "for" loop
#endif // PISTACHE_USE_SSL

        for (;;)
        {
            Fd conn_fd = connection->fdDirectOrFromSsl();
            if (conn_fd == PS_FD_EMPTY)
                break; // can happen if fd was closed meanwhile

            PST_SSIZE_T bytes = -1;
#ifdef PISTACHE_USE_SSL
            if (connection->isSsl())
                bytes = connection->fdOrSslConn()->getSslConn()->sslRawRecv(
                    buffer + totalBytes, max_buffer - totalBytes,
                    know_readable);
            else
#endif // PISTACHE_USE_SSL
                bytes = PST_SOCK_RECV(
                    GET_ACTUAL_FD(conn_fd), buffer + totalBytes,
                    max_buffer - totalBytes, 0);

#ifdef PISTACHE_USE_SSL
            know_readable = false;
#endif // PISTACHE_USE_SSL

            if (bytes == -1)
            {
                if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
                {
                    if (totalBytes)
                    {
                        PS_LOG_DEBUG_ARGS("Passing %d totalBytes to "
                                          "handleResponsePacket",
                                          totalBytes);

                        connection->handleResponsePacket(buffer, totalBytes);
                    }
                    else
                    {
                        PS_LOG_DEBUG("totalBytes is zero");
                    }
                }
                else
                {
                    PST_DECL_SE_ERR_P_EXTRA;
                    const char* err_msg = PST_STRERROR_R_ERRNO;
                    PS_LOG_DEBUG_ARGS("recv err, errno %d %s", errno, err_msg);

                    connection->handleError(err_msg);
                }
                break;
            }
            else if (bytes == 0)
            {
                if (totalBytes == 0)
                {
                    connection->handleError("Remote closed connection");
                }
                else
                {
                    PS_LOG_DEBUG_ARGS("Passing %d totalBytes to "
                                      "handleResponsePacket",
                                      totalBytes);

                    connection->handleResponsePacket(buffer, totalBytes);
                }

                connections.erase(conn_fd);
                connection->closeFromRemoteClosedConnection();
                break;
            }
            else
            {
                PS_LOG_DEBUG_ARGS("Rxed %d bytes", bytes);
                totalBytes += bytes;
            }
            if (totalBytes >= max_buffer)
            {
                auto new_max_buffer = (max_buffer * 2);
                char* new_buffer    = 0;
                if ((new_max_buffer > max_max_buffer) || (nullptr == (new_buffer = new char[new_max_buffer + 16])))
                {
                    if (new_max_buffer > max_max_buffer)
                        PS_LOG_WARNING("Receive buffer would be too big");
                    else
                        PS_LOG_WARNING_ARGS("Failed to alloc %d bytes memory",
                                            new_max_buffer + 16);

                    connection->handleResponsePacket(buffer, totalBytes);
                    break;
                }
                std::memcpy(new_buffer, buffer, max_buffer);
                buffer_uptr = std::unique_ptr<char[]>(new_buffer);
                buffer      = new_buffer;
                max_buffer  = new_max_buffer;
            }
        }
    }

    Connection::Connection(size_t maxResponseSize)
        : requestEntry(nullptr)
        , parser(maxResponseSize)
    {
        state_.store(static_cast<uint32_t>(State::Idle));
        connectionState_.store(NotConnected);
    }

    void Connection::connect(Address::Scheme scheme,
#ifdef PISTACHE_USE_SSL
                             SslVerification sslVerification,
#endif // PISTACHE_USE_SSL
                             const std::string& domain,
                             const std::string* page)
    {
        const Address addr(helpers::httpAddr(
            domain,
            (Address::Scheme::Https == scheme) ? 443 : 0, // default port
            scheme, page));

#ifdef PISTACHE_USE_SSL
        if (scheme == Address::Scheme::Https)
        {
            std::string domain_without_port(domain);
            size_t last_colon = domain.find_last_of(':');
            if (last_colon != std::string::npos)
                domain_without_port = domain.substr(0, last_colon);

            connectSsl(addr, domain_without_port, sslVerification);
        }
        else
#endif // PISTACHE_USE_SSL
        {
            connectSocket(addr);
        }
    }

    void Connection::connectSocket(const Address& addr)
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

        em_socket_t sfd = -1;

        for (const addrinfo* an_addr = addrs; an_addr;
             an_addr                 = an_addr->ai_next)
        {
            sfd = PST_SOCK_SOCKET(an_addr->ai_family, an_addr->ai_socktype,
                                  an_addr->ai_protocol);
            PS_LOG_DEBUG_ARGS("::socket actual_fd %d", sfd);
            if (sfd < 0)
                continue;

            make_non_blocking(sfd);

            connectionState_.store(Connecting);

            { // encapsulate
                Fd fd = PS_FD_EMPTY;
#ifdef _USE_LIBEVENT
                // We're openning a connection to a remote resource - I guess
                // it makes sense to allow either read or write
                fd = TRY_NULL_RET(
                    EventMethFns::em_event_new(
                        sfd, // pre-allocated file desc
                        EVM_READ | EVM_WRITE | EVM_PERSIST | EVM_ET,
                        F_SETFDL_NOTHING, // setfd
                        PST_O_NONBLOCK // setfl
                        ));
#else
                fd = sfd;
#endif
                fd_or_ssl_conn_ = std::make_shared<FdOrSslConn>(fd);
            }

            transport_
                ->asyncConnect(shared_from_this(), an_addr->ai_addr,
                               static_cast<PST_SOCKLEN_T>(an_addr->ai_addrlen))
                // Note: We cast to PST_SOCKLEN_T for Windows because Windows
                // uses "int" for PST_SOCKLEN_T, whereas Linux uses size_t. In
                // general, even for Windows we use size_t for addresses'
                // lengths in Pistache (e.g. in struct ifaddr), hence why we
                // cast here
                .then(
                    [sfd, this]() {
                        socklen_t len = sizeof(saddr);
                        PST_SOCK_GETSOCKNAME(sfd, reinterpret_cast<struct sockaddr*>(&saddr), &len);
                        connectionState_.store(Connected);
                        processRequestQueue();
                    },
                    PrintException());
            break;
        }

        if (sfd < 0)
            throw std::runtime_error("Failed to connect");
    }

#ifdef PISTACHE_USE_SSL
    std::mutex Connection::hostChainPemFileMutex_;
    std::string Connection::hostChainPemFile_;
    const std::string& Connection::getHostChainPemFile()
    {
        GUARD_AND_DBG_LOG(hostChainPemFileMutex_);
        return (hostChainPemFile_);
    }
    void Connection::setHostChainPemFile(const std::string& _hostCPFl) // call once
    {
        GUARD_AND_DBG_LOG(hostChainPemFileMutex_);
        hostChainPemFile_ = _hostCPFl;
    }
#endif // PISTACHE_USE_SSL

#ifdef PISTACHE_USE_SSL
    void Connection::connectSsl(const Address& addr, const std::string& domain,
                                SslVerification sslVerification)
    {
        PS_TIMEDBG_START_THIS;

        const std::string host_cpem_file(getHostChainPemFile());

        bool do_verification = (sslVerification != SslVerification::Off);
        if (sslVerification == SslVerification::OnExceptLocalhost)
        {
            std::string domain_lwr(domain);
            std::transform(domain.begin(), domain.end(), domain_lwr.begin(),
                           [](const char ch) {
                               const unsigned char uch = static_cast<unsigned char>(ch);
                               auto ires               = ::tolower(uch);
                               return (static_cast<char>(ires));
                           });
            if (domain_lwr.compare("localhost") == 0)
                do_verification = false;
        }

        std::shared_ptr<SslConnection> ssl_conn(
            std::make_shared<SslConnection>(domain,
                                            addr.port(),
                                            addr.family(), // domain
                                            addr.page(),
                                            do_verification,
                                            &host_cpem_file));
        if (!ssl_conn)
            throw std::runtime_error("Failed to connect");

        std::shared_ptr<FdOrSslConn> fd_or_ssl_conn_new(
            std::make_shared<FdOrSslConn>(ssl_conn));
        if (!fd_or_ssl_conn_new)
            throw std::runtime_error("Failed to connect");

        connectionState_.store(Connecting);
        fd_or_ssl_conn_ = fd_or_ssl_conn_new;

        transport_->asyncConnect(shared_from_this(),
                                 NULL /*sockaddr*/, 0 /*addr_len*/)
            .then([=]() {
                                     connectionState_.store(Connected);
                                     processRequestQueue(); },
                  PrintException());

        if (fdDirectOrFromSsl() == PS_FD_EMPTY)
            throw std::runtime_error("Failed to connect");
    }
#endif // PISTACHE_USE_SSL

    std::string Connection::dump() const
    {
        std::ostringstream oss;
        oss << "Connection(fd = " << fdDirectOrFromSsl() << ", src_port = ";
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
            Pistache::details::unreachable();
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

            if (fd_or_ssl_conn_)
                fd_or_ssl_conn_->close();

            PS_LOG_DEBUG_ARGS("Unlocking handling_mutex %p",
                              &handling_mutex);
        }
        else
        {
            PS_LOG_DEBUG_ARGS("Closing connection %p without transport", this);

            connectionState_.store(NotConnected);
            if (fd_or_ssl_conn_)
                fd_or_ssl_conn_->close();
        }
    }

    // closeFromRemoteClosedConnection is called from Transport::handleIncoming
    // when the remote does a zero-size send (bytes == 0), which means that the
    // remote has cleanly closed the connection.
    //
    // handling mutex already locked
    void Connection::closeFromRemoteClosedConnection()
    {
        PS_TIMEDBG_START_THIS;

        // Note: We don't call transport_->setStopHandlingwMutexAlreadyLocked;
        // this is a clean shutdown of this one connection, we don't need to
        // stop all handling on the transport

        connectionState_.store(NotConnected);

        if (fd_or_ssl_conn_)
            fd_or_ssl_conn_->close();
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
            PS_LOG_DEBUG_ARGS("Parser exception, totalBytes %d, buffer %s",
                              totalBytes, buffer);
            handleError(ex.what());
        }
    }

    void Connection::handleError(const char* error)
    {
        PS_TIMEDBG_START_THIS;

        PS_LOG_DEBUG_ARGS("Error string %s", error);

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
            [&, this](Async::Resolver& resolve, Async::Rejection& reject) {
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
            [&, this](Async::Resolver& resolve, Async::Rejection& reject) {
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

    void ConnectionPool::init(size_t maxConnectionsPerHostParm,
                              size_t maxResponseSizeParm)
    {
        this->maxConnectionsPerHost = maxConnectionsPerHostParm;
        this->maxResponseSize       = maxResponseSizeParm;
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

#ifdef PISTACHE_USE_SSL
    Client::Options& Client::Options::clientSslVerification(
        SslVerification val)
    {
        clientSslVerification_ = val;
        return *this;
    }
#endif // PISTACHE_USE_SSL

    Client::Client()
        : reactor_(Aio::Reactor::create())
        , pool()
        , transportKey()
#ifdef PISTACHE_USE_SSL
        , sslVerification(SslVerification::On)
#endif // PISTACHE_USE_SSL
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
#ifdef PISTACHE_USE_SSL
        sslVerification = options.clientSslVerification_;
#endif // PISTACHE_USE_SSL
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
        auto resourceData = request.resource();

        PS_LOG_DEBUG_ARGS("resourceData %s", resourceData.c_str());

        bool https_url = false;
        auto resource  = splitUrl(resourceData, true, &https_url);
        // For splitUrl, true => DO remove subdomain (e.g. www.) from host name
        PS_LOG_DEBUG_ARGS("URL is %s", https_url ? "HTTPS" : "HTTP");

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

                auto transport = std::static_pointer_cast<Transport>(transports[static_cast<unsigned int>(index)]);
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

                PS_LOG_DEBUG_ARGS("Connection %p calling connect", conn.get());
                const std::string domain(resource.first);
                const std::string page(resource.second);

                conn->connect(https_url ? Address::Scheme::Https : Address::Scheme::Http,
#ifdef PISTACHE_USE_SSL
                              https_url ? sslVerification : SslVerification::Off,
#endif // PISTACHE_USE_SSL
                              domain, &page);
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

    Fd FdOrSslConn::getFd() const
    {
#ifdef PISTACHE_USE_SSL
        return (ssl_conn_ ? ssl_conn_->getFd() : fd_);
#else
        return (fd_);
#endif // PISTACHE_USE_SSL... else...
    }

    void FdOrSslConn::close()
    {
        if (fd_ != PS_FD_EMPTY)
        {
            CLOSE_FD(fd_);
            fd_ = PS_FD_EMPTY;
        }
#ifdef PISTACHE_USE_SSL
        if (ssl_conn_)
        {
            ssl_conn_->close();
            ssl_conn_ = nullptr;
        }
#endif // PISTACHE_USE_SSL
    }

} // namespace Pistache::Http::Experimental
