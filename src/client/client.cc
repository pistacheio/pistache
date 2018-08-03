/* 
   Mathieu Stefani, 29 janvier 2016
   
   Implementation of the Http client
*/

#include <algorithm>

#include <sys/sendfile.h>
#include <netdb.h>

#include <pistache/client.h>
#include <pistache/stream.h>
#include <pistache/description.h>
#include <algorithm>
#include <sys/sendfile.h>
#include <netdb.h>

namespace Pistache {

using namespace Polling;

namespace Http {

namespace {
    Address httpAddr(const StringView& view) {
        auto str = view.toString();
        auto pos = str.find(':');
        if (pos == std::string::npos) {
            return Address(std::move(str), 80);
        }

        auto host = str.substr(0, pos);
        auto port = std::stoi(str.substr(pos + 1));
        return Address(std::move(host), port);
    }
}

static constexpr const char* UA = "pistache/0.1";

class UrlParts
{
private:
    Rest::Scheme mScheme;
    StringView mHost;
    uint16_t mPort;
    StringView mPage;

public:    
    UrlParts(Rest::Scheme _scheme, const StringView & _host,
             uint16_t _port,
             const StringView & _page) :
        mScheme(_scheme), mHost(_host), mPort(_port), mPage(_page) { }

    Rest::Scheme getScheme() const { return(mScheme); }
    const StringView & getHost() const { return(mHost); }
    uint16_t getHostPort() const { return(mPort); }
    const StringView & getPage() const { return(mPage); }

    std::string getHostAndPortAsStr() const
        { std::string res(mHost.toString());
          res.append(":" + std::to_string(mPort)); return(res); }

    std::string getHostAndPortIfNotDefAsStr() const
        { std::string res(mHost.toString());
            if ((mPort != 80) && (mPort != 443) && (mPort != 0))
                res.append(":" + std::to_string(mPort));
          return(res); }
};

UrlParts
splitUrl(const std::string& url) {
    RawStreamBuf<char> buf(const_cast<char *>(&url[0]), url.size());
    StreamCursor cursor(&buf);

    Rest::Scheme scheme;

    if (match_string("https://", std::strlen("https://"), cursor))
    {
        scheme = Rest::Scheme::Https;
    }
    else if (match_string("http://", std::strlen("http://"), cursor))
    {
        scheme = Rest::Scheme::Http;
    }

    // !!!! Add port number parsing

    /*
     * Google seems to want "www.googleapis.com" as host
    match_string("www", std::strlen("www"), cursor);
    match_literal('.', cursor);
     */

    StreamCursor::Token hostToken(cursor);
    match_until({ '?', '/' }, cursor);

    StringView host(hostToken.rawText(), hostToken.size());
    StringView page(cursor.offset(), buf.endptr());

    return(UrlParts(scheme, host, 443, page));
}

struct ExceptionPrinter {
    void operator()(std::exception_ptr exc) {
        try {
            std::rethrow_exception(exc);
        } catch (const std::exception& e) {
            std::cout << "Got exception: " << e.what() << std::endl;
        }
    }
};

namespace {
    #define OUT(...) \
    do { \
        __VA_ARGS__; \
        if (!os)  return false; \
    } while (0)

    template<typename H, typename Stream, typename... Args>
    typename std::enable_if<Http::Header::IsHeader<H>::value, Stream&>::type
    writeHeader(Stream& stream, Args&& ...args) {
        using Http::crlf;

        H header(std::forward<Args>(args)...);

        stream << H::Name << ": ";
        header.write(stream);

        stream << crlf;

        return stream;
    }

    bool writeHeaders(const Http::Header::Collection& headers, DynamicStreamBuf& buf) {
        using Http::crlf;

        std::ostream os(&buf);

        for (const auto& header: headers.list()) {
            OUT(os << header->name() << ": ");
            OUT(header->write(os));
            OUT(os << crlf);
        }

        return true;
    }

    bool writeCookies(const Http::CookieJar& cookies, DynamicStreamBuf& buf) {
        using Http::crlf;

        std::ostream os(&buf);
        for (const auto& cookie: cookies) {
            OUT(os << "Cookie: ");
            OUT(cookie.write(os));
            OUT(os << crlf);
        }

        return true;

    }

    bool writeRequest(const Http::Request& request, DynamicStreamBuf &buf) {
        using Http::crlf;

        std::ostream os(&buf);

        auto res = request.resource();
        auto s = splitUrl(res);
        auto body = request.body();

        auto host_and_maybe_port = s.getHostAndPortIfNotDefAsStr();
        auto path = s.getPage();

        auto pathStr = path.toString();

        OUT(os << request.method() << " ");
        if (pathStr[0] != '/')
            OUT(os << '/');
        OUT(os << pathStr);
        OUT(os << " HTTP/1.1" << crlf);

        if (!writeCookies(request.cookies(), buf)) return false;
        if (!writeHeaders(request.headers(), buf)) return false;

        if (!writeHeader<Http::Header::UserAgent>(os, UA)) return false;
        if (!writeHeader<Http::Header::Host>(os, host_and_maybe_port)) return false;
        if (!body.empty()) {
            if (!writeHeader<Http::Header::ContentLength>(os, body.size())) return false;
        }
        OUT(os << crlf);

        if (!body.empty()) {
            OUT(os << body);
        }

        return true;


    }

#undef OUT
}

void
Transport::onReady(const Aio::FdSet& fds) {
    for (const auto& entry: fds) {
        if (entry.getTag() == connectionsQueue.tag()) {
            handleConnectionQueue();
        }
        else if (entry.getTag() == requestsQueue.tag()) {
            handleRequestsQueue();
        }

        else if (entry.isReadable()) {
            auto tag = entry.getTag();
            auto fd = tag.value();
            auto reqIt = connections.find(fd);
            if (reqIt != std::end(connections))
                handleIncoming(reqIt->second.connection);
            else {
                auto timerIt = timeouts.find(fd);
                if (timerIt != std::end(timeouts))
                    handleTimeout(timerIt->second);
                else {
                    throw std::runtime_error("Unknown fd");
                }
            }
        }
        else {
            auto tag = entry.getTag();
            auto fd = tag.value();
            auto connIt = connections.find(fd);
            if (connIt != std::end(connections)) {
                auto& conn = connIt->second;
                if (entry.isHangup())
                    conn.reject(Error::system("Could not connect"));
                else {

                    #ifdef PIST_INCLUDE_SSL
                    if (conn.connection->isSsl())
                    { // Complete SSL verification
                        try {
                            std::shared_ptr<SslConnection> ssl_conn(
                                           conn.connection->dfd->getSslConn());
                            if (!ssl_conn)
                                throw std::runtime_error("Null ssl_conn");
                        }
                        catch(...)
                        {
                            conn.reject(Error::system(
                                            "SSL failure, could not connect"));
                            throw std::runtime_error(
                                "SSL failure, could not connect");
                        }
                    }
                    #endif
                    
                    conn.resolve();
                    // We are connected, we can start reading data now
                    reactor()->modifyFd(key(), conn.connection->getFd(),
                                        NotifyOn::Read);
                }
            } else {
                throw std::runtime_error("Unknown fd");
            }
        }
    }
}

void
Transport::registerPoller(Polling::Epoll& poller) {
    requestsQueue.bind(poller);
    connectionsQueue.bind(poller);
}

Async::Promise<void>
Transport::asyncConnect(const std::shared_ptr<Connection>& connection, const struct sockaddr* address, socklen_t addr_len)
{
    return Async::Promise<void>([=](Async::Resolver& resolve, Async::Rejection& reject) {
        ConnectionEntry entry(std::move(resolve), std::move(reject), connection, address, addr_len);
        auto *e = connectionsQueue.allocEntry(std::move(entry));
        connectionsQueue.push(e);
    });
}

Async::Promise<ssize_t>
Transport::asyncSendRequest(
        const std::shared_ptr<Connection>& connection,
        std::shared_ptr<TimerPool::Entry> timer,
        const Buffer& buffer) {

    return Async::Promise<ssize_t>([&](Async::Resolver& resolve, Async::Rejection& reject) {
        auto ctx = context();
        if (std::this_thread::get_id() != ctx.thread()) {
            RequestEntry req(std::move(resolve), std::move(reject), connection, std::move(timer), buffer.detach());
            auto *e = requestsQueue.allocEntry(std::move(req));
            requestsQueue.push(e);
        } else {
            RequestEntry req(std::move(resolve), std::move(reject), connection, std::move(timer), buffer);

            asyncSendRequestImpl(req);
        }
    });
}


void
Transport::asyncSendRequestImpl(
        const RequestEntry& req, WriteStatus status)
{
    auto buffer = req.buffer;

    auto cleanUp = [&]() {
        if (buffer.isOwned) delete[] buffer.data;
    };

    auto conn = req.connection;
    if (!conn)
        throw(std::runtime_error("Failed to get conn"));

    #ifdef PIST_INCLUDE_SSL
    DualFdSPtr dfd(conn->getDfd());
    if (!dfd)
        throw(std::runtime_error("Failed to get dfd"));
    #endif

    auto fd = conn->getFd();
    if (fd <= 0)
        throw(std::runtime_error("Failed to get fd"));
    
    ssize_t totalWritten = 0;
    for (;;) {
        ssize_t bytesWritten = 0;
        auto len = buffer.len - totalWritten;
        auto ptr = buffer.data + totalWritten;
        bytesWritten =
            #ifdef PIST_INCLUDE_SSL
            conn->isSsl() ? dfd->getSslConn()->sslRawSend(ptr, len) :
            // Note for use with sendWithIncreasingDelays: Don't need retries /
            // increasing delays for SSL, handled in SSL code
            #endif
            ::send(fd, ptr, len, 0);

        if (bytesWritten < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (status == FirstTry) {
                    throw std::runtime_error("Unimplemented, fix me!");
                }
                reactor()->modifyFd(key(), fd, NotifyOn::Write, Polling::Mode::Edge);
            }
            else {
                cleanUp();
                req.reject(Error::system("Could not send request"));
            }
            break;
        }
        else {
            totalWritten += bytesWritten;
            if (totalWritten == len) {
                cleanUp();
                if (req.timer) {
                    timeouts.insert(
                          std::make_pair(req.timer->fd, conn));
                    req.timer->registerReactor(key(), reactor());
                }
                req.resolve(totalWritten);
                break;
            }
        }
    }
}

void
Transport::handleRequestsQueue() {
    // Let's drain the queue
    for (;;) {
        auto entry = requestsQueue.popSafe();
        if (!entry) break;

        auto &req = entry->data();
        asyncSendRequestImpl(req);
    }
}

void
Transport::handleConnectionQueue() {
    for (;;) {
        auto entry = connectionsQueue.popSafe();
        if (!entry) break;


        auto &data = entry->data();
        const auto& conn = data.connection;
        if (!conn)
            continue;

        Aio::Reactor * reactr(reactor());
        if (!reactr)
            throw std::runtime_error("Null reactor for transport");

        Aio::Reactor::Key reactor_key(key());

        int conn_res = conn->doConnect(*reactr, reactor_key, data);
        if (conn_res == 0)
        {
            Fd fd = conn->getFd();
            if (fd <= 0)
                data.reject(Error::system("Failed to connect"));
            else
                connections.insert(std::make_pair(fd, std::move(data)));
        }
        else
        {
            data.reject(Error::system("Failed to connect"));
        }
    }
}

void
Transport::handleIncoming(const std::shared_ptr<Connection>& connection) {
    char buffer[Const::MaxBuffer];
    memset(buffer, 0, sizeof buffer);

    if (!connection)
        throw(std::runtime_error("Null connection"));

    #ifdef PIST_INCLUDE_SSL
    DualFdSPtr dfd(connection->getDfd());
    if (!dfd)
        throw(std::runtime_error("Failed to get dfd"));
    #endif

    ssize_t totalBytes = 0;
    for (;;) {

        ssize_t bytes;

        bytes =
            #ifdef PIST_INCLUDE_SSL
              connection->isSsl() ?
                 dfd->getSslConn()->sslRawRecv(
                           buffer+ totalBytes, Const::MaxBuffer - totalBytes) :
            #endif
              recv(connection->getFd(), buffer + totalBytes,
                   Const::MaxBuffer - totalBytes, 0);
        
        if (bytes == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (totalBytes > 0) {
                    handleResponsePacket(connection, buffer, totalBytes);
                }
            } else {
                connection->handleError(strerror(errno));
            }
            break;
        }
        else if (bytes == 0) {
            if (totalBytes > 0) {
                handleResponsePacket(connection, buffer, totalBytes);
            } else {
                connection->handleError("Remote closed connection");
            }
            connections.erase(connection->getFd());
            connection->close();
            break;
        }

        else {
            totalBytes += bytes;
            if (totalBytes > Const::MaxBuffer) {
                std::cerr << "Too long packet" << std::endl;
                break;
            }
        }
    }
}

void
Transport::handleResponsePacket(const std::shared_ptr<Connection>& connection, const char* buffer, size_t totalBytes) {
    connection->handleResponsePacket(buffer, totalBytes);
}

void
Transport::handleTimeout(const std::shared_ptr<Connection>& connection) {
    connection->handleTimeout();
}

void
Connection::connect(Aio::Reactor & _reactor, Aio::Reactor::Key & _key,
                    UrlParts & _urlParts)
{
    #ifdef PIST_INCLUDE_SSL
    if (_urlParts.getScheme() == Rest::Scheme::Https)
    {
        connectSsl(_reactor, _key, _urlParts);
        return;
    }
    #endif
    
    Pistache::Address addr(httpAddr(_urlParts.getHost()));
    connectSocket(addr);
}

#ifdef PIST_INCLUDE_SSL
void
Connection::connectSsl(Aio::Reactor & _reactor, Aio::Reactor::Key & _key,
                       UrlParts & _urlParts)
{
    const std::string host_cpem_file(getHostChainPemFile());
        
    std::shared_ptr<SslConnection> ssl_conn(std::make_shared<SslConnection>
                                            (_urlParts.getHost(),
                                             _urlParts.getHostPort(),
                                             _urlParts.getPage(),
                                             &host_cpem_file));
    if (!ssl_conn)
        throw std::runtime_error("Failed to connect");
    
    DualFdSPtr dfd_new(std::make_shared<DualFd>(ssl_conn));
    if (!dfd_new)
        throw std::runtime_error("Failed to connect");

    setConnecting();
    dfd = dfd_new;

    transport_->asyncConnect(shared_from_this(),
                            NULL/*sockaddr*/, 0/*addr_len*/).then([=]() {
               this->setConnectedProcessRequestQueue(); }, ExceptionPrinter());

    /*
    // For conventional sockets, code does this out of Transport::onReady upon
    // receiving a "connected" event
    _reactor.registerFdOneShot(_key, getFd(),
                      NotifyOn::Write | NotifyOn::Hangup | NotifyOn::Shutdown);
    
    // We are connected, we can start reading data now
    _reactor.modifyFd(_key, getFd(), NotifyOn::Read);
    */

    if (getFd() <= 0)
        throw std::runtime_error("Failed to connect");
        
}
#endif

void
Connection::connectSocket(Pistache::Address addr)
{
struct addrinfo hints;
    struct addrinfo *addrs;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* Stream socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0;  

    auto host = addr.host();

    /* We rely on the fact that a string literal is an lvalue const char[N] */
    static constexpr size_t MaxPortLen = sizeof("65535");

    char port[MaxPortLen];
    std::fill(port, port + MaxPortLen, 0);
    std::snprintf(port, MaxPortLen, "%d", static_cast<uint16_t>(addr.port()));

    TRY(::getaddrinfo(host.c_str(), port, &hints, &addrs));

    int sfd = -1;

    for (struct addrinfo *addr = addrs; addr; addr = addr->ai_next) {
        sfd = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (sfd < 0) continue;

        make_non_blocking(sfd);

        setConnecting();
        dfd = std::make_shared<DualFd>(sfd);

        transport_->asyncConnect(shared_from_this(), addr->ai_addr, addr->ai_addrlen)
            .then([=]() { 
                socklen_t len = sizeof(saddr);
                getsockname(sfd, (struct sockaddr *)&saddr, &len);

                this->setConnectedProcessRequestQueue();
            }, ExceptionPrinter());
        break;

    }

    if (sfd < 0)
        throw std::runtime_error("Failed to connect");
}

#ifdef PIST_INCLUDE_SSL
std::string Connection::mHostChainPemFile;
const std::string & Connection::getHostChainPemFile()
    {return(mHostChainPemFile);}
void Connection::setHostChainPemFile(const std::string & _hostCPFl)//call once
    {mHostChainPemFile = _hostCPFl;}
#endif    
    
int
Connection::doConnect(Aio::Reactor & _reactor, Aio::Reactor::Key & _key,
                      ConnectionEntry & _connEntry)
{
    #ifdef PIST_INCLUDE_SSL
    if ((_connEntry.connection) && ((_connEntry.connection)->isSsl()))
    {
        std::shared_ptr<SslConnection> ssl_conn(
                                     _connEntry.connection->dfd->getSslConn());
        if (!ssl_conn)
        {
            _connEntry.reject(Error::system("Failed to connect,NULL ssl_con"));
            return(-1);
        }
        
        _reactor.registerFdOneShot(_key, getFd(),
                      NotifyOn::Write | NotifyOn::Hangup | NotifyOn::Shutdown);

        // connected synchronously
        _connEntry.resolve();

        // We are actually already connected, we can start reading data now
        _reactor.modifyFd(_key, getFd(), NotifyOn::Read);
        return(0);
    }
    #endif

    int res = ::connect(getFd(), _connEntry.addr, _connEntry.addr_len);
    if (res == -1) {
        if (errno == EINPROGRESS) {
            _reactor.registerFdOneShot(_key, getFd(), NotifyOn::Write | NotifyOn::Hangup | NotifyOn::Shutdown);
        }
        else {
            _connEntry.reject(Error::system("Failed to connect"));
            return(-1);
        }
    }

    return(0);
    // Caller will insert successful connection into connections set on return
}

std::string
Connection::dump() const {
    std::ostringstream oss;
    oss << "Connection(fd = " << getFd() << ", src_port = ";
    oss << ntohs(saddr.sin_port) << ")";
    return oss.str();
}

bool
Connection::isIdle() const {
    return static_cast<Connection::State>(state_.load()) == Connection::State::Idle;
}

bool
Connection::isConnected() const {
    std::lock_guard<std::mutex> grd(connectionStateMutex_);
    return connectionState_ == Connected;
}

bool Connection::isConnectedPushReqEntryIfNot(Async::Resolver & resolve,
                                      Async::Rejection & reject,
                                      const Http::Request& request,
                                      std::chrono::milliseconds timeout,
                                      const OnDone & onDone)
{
    std::lock_guard<std::mutex> grd(connectionStateMutex_);

    if (connectionState_ == Connected)
        return(true);

    auto* entry = requestsQueue.allocEntry(RequestData(
                                               std::move(resolve),
                                               std::move(reject),
                                               request,
                                               timeout,
                                               std::move(onDone)));
    
    requestsQueue.push(entry);
    return(false);
}

void Connection::setConnectedProcessRequestQueue()
{
    std::lock_guard<std::mutex> grd(connectionStateMutex_);
    connectionState_ = Connected;
    processRequestQueue();
}    

void Connection::setConnecting()
{
    std::lock_guard<std::mutex> grd(connectionStateMutex_);
    connectionState_ = Connecting;
}

void
Connection::close() {
    std::lock_guard<std::mutex> grd(connectionStateMutex_);
    
    connectionState_ = NotConnected;
    if (dfd)
        dfd->close();
}

void
Connection::associateTransport(const std::shared_ptr<Transport>& transport) {
    if (transport_)
        throw std::runtime_error("A transport has already been associated to the connection");

    transport_ = transport;
}

bool
Connection::hasTransport() const {
    return transport_ != nullptr;
}

void
Connection::handleResponsePacket(const char* buffer, size_t bytes) {

    parser_.feed(buffer, bytes);
    if (parser_.parse() == Private::State::Done) {
        if (!inflightRequests.empty()) {
            auto req = std::move(inflightRequests.front());
            inflightRequests.pop_back();

            if (req.timer) {
                req.timer->disarm();
                timerPool_.releaseTimer(req.timer);
            }

            req.resolve(std::move(parser_.response));
            if (req.onDone)
                req.onDone();
        }
        
        parser_.reset();
    }
}

void
Connection::handleError(const char* error) {
    if (!inflightRequests.empty()) {
        auto req = std::move(inflightRequests.front());
        if (req.timer) {
            req.timer->disarm();
            timerPool_.releaseTimer(req.timer);
        }

        req.reject(Error(error));
        if (req.onDone)
            req.onDone();
    }
}

void
Connection::handleTimeout() {
    if (!inflightRequests.empty()) {
        auto req = std::move(inflightRequests.front());
        inflightRequests.pop_back();

        timerPool_.releaseTimer(req.timer);

        if (req.onDone)
            req.onDone();
        /* @API: create a TimeoutException */
        req.reject(std::runtime_error("Timeout"));
    }
}


Async::Promise<Response>
Connection::perform(
        const Http::Request& request,
        std::chrono::milliseconds timeout,
        OnDone onDone) {
    return Async::Promise<Response>([=](Async::Resolver& resolve, Async::Rejection& reject) {
        if (!isConnected()) {
            auto* entry = requestsQueue.allocEntry(
                RequestData(
                    std::move(resolve),
                    std::move(reject),
                    request,
                    timeout,
                    std::move(onDone)));
            requestsQueue.push(entry);
        } else {
            performImpl(request, timeout, std::move(resolve), std::move(reject), std::move(onDone));
        }
    });
}

/**
 * This class is used to emulate the generalized lambda capture feature from C++14
 * whereby a given object can be moved inside a lambda, directly from the capture-list
 *
 * So instead, it will use the exact same semantic than auto_ptr (don't beat me for that),
 * meaning that it will move the value on copy
 */
template<typename T>
struct MoveOnCopy {
    MoveOnCopy(T val)
        : val(std::move(val))
    { }

    MoveOnCopy(MoveOnCopy& other)
        : val(std::move(other.val))
    { }

    MoveOnCopy& operator=(MoveOnCopy& other) {
        val = std::move(other.val);
    }

    MoveOnCopy(MoveOnCopy&& other) = default;
    MoveOnCopy& operator=(MoveOnCopy&& other) = default;

    operator T&&() {
        return std::move(val);
    }

    T val;
};

template<typename T>
MoveOnCopy<T> make_copy_mover(T arg) {
    return MoveOnCopy<T>(std::move(arg));
}

void
Connection::performImpl(
        const Http::Request& request,
        std::chrono::milliseconds timeout,
        Async::Resolver resolve,
        Async::Rejection reject,
        OnDone onDone) {

    DynamicStreamBuf buf(128);

    if (!writeRequest(request, buf))
        reject(std::runtime_error("Could not write request"));

    auto buffer = buf.buffer();
    std::shared_ptr<TimerPool::Entry> timer(nullptr);
    if (timeout.count() > 0) {
        timer = timerPool_.pickTimer();
        timer->arm(timeout);
    }

    auto rejectClone = reject.clone();
    auto rejectCloneMover = make_copy_mover(std::move(rejectClone));

    // Move the resolver and rejecter inside the lambda
    auto resolveMover = make_copy_mover(std::move(resolve));
    auto rejectMover = make_copy_mover(std::move(reject));


    transport_->asyncSendRequest(shared_from_this(), timer, buffer).then(
        [=](ssize_t bytes) mutable {
            inflightRequests.push_back(RequestEntry(std::move(resolveMover), std::move(rejectMover), std::move(timer), std::move(onDone)));
        },
        [=](std::exception_ptr e) { rejectCloneMover.val(e); });
}

void
Connection::processRequestQueue() {
    for (;;) {
        auto entry = requestsQueue.popSafe();
        if (!entry) break;

        auto &req = entry->data();
        performImpl(
                req.request,
                req.timeout, std::move(req.resolve), std::move(req.reject), std::move(req.onDone));
    }

}

void
ConnectionPool::init(size_t maxConnsPerHost) {
    maxConnectionsPerHost = maxConnsPerHost;
}

std::shared_ptr<Connection>
ConnectionPool::pickConnection(const std::string& domain) {
    Connections pool;

    {
        Guard guard(connsLock);
        auto poolIt = conns.find(domain);
        if (poolIt == std::end(conns)) {
            Connections connections;
            for (size_t i = 0; i < maxConnectionsPerHost; ++i) {
                connections.push_back(std::make_shared<Connection>());
            }

            poolIt = conns.insert(std::make_pair(domain, std::move(connections))).first;
        }
        pool = poolIt->second;
    }

    for (auto& conn: pool) {
        auto& state = conn->state_;
        auto curState = static_cast<uint32_t>(Connection::State::Idle);
        auto newState = static_cast<uint32_t>(Connection::State::Used);
        if (state.compare_exchange_strong(curState, newState)) {
            return conn;
        }
    }

    return nullptr;
}

void
ConnectionPool::releaseConnection(const std::shared_ptr<Connection>& connection) {
    connection->state_.store(static_cast<uint32_t>(Connection::State::Idle));
}

size_t
ConnectionPool::usedConnections(const std::string& domain) const {
    Connections pool;
    {
        Guard guard(connsLock);
        auto it = conns.find(domain);
        if (it == std::end(conns)) {
            return 0;
        }
        pool = it->second;
    }

    return std::count_if(pool.begin(), pool.end(), [](const std::shared_ptr<Connection>& conn) {
        return conn->isConnected();
    });
}

size_t
ConnectionPool::idleConnections(const std::string& domain) const {
    Connections pool;
    {
        Guard guard(connsLock);
        auto it = conns.find(domain);
        if (it == std::end(conns)) {
            return 0;
        }
        pool = it->second;
    }

    return std::count_if(pool.begin(), pool.end(), [](const std::shared_ptr<Connection>& conn) {
        return conn->isIdle();
    });
}

size_t
ConnectionPool::availableConnections(const std::string& domain) const {
    return 0;
}

void
ConnectionPool::closeIdleConnections(const std::string& domain) {
}

RequestBuilder&
RequestBuilder::method(Method method)
{
    request_.method_ = method;
    return *this;
}

RequestBuilder&
RequestBuilder::resource(std::string val) {
    request_.resource_ = std::move(val);
    return *this;
}

RequestBuilder&
RequestBuilder::params(const Uri::Query& params) {
    request_.query_ = params;
    return *this;
}

RequestBuilder&
RequestBuilder::header(const std::shared_ptr<Header::Header>& header) {
    request_.headers_.add(header);
    return *this;
}

RequestBuilder&
RequestBuilder::cookie(const Cookie& cookie) {
    request_.cookies_.add(cookie);
    return *this;
}

RequestBuilder&
RequestBuilder::body(std::string val) {
    request_.body_ = std::move(val);
    return *this;
}

Async::Promise<Response>
RequestBuilder::send() {
    return client_->doRequest(std::move(request_), timeout_);
}

Client::Options&
Client::Options::threads(int val) {
    threads_ = val;
    return *this;
}

Client::Options&
Client::Options::keepAlive(bool val) {
    keepAlive_ = val;
    return *this;
}

Client::Options&
Client::Options::maxConnectionsPerHost(int val) {
    maxConnectionsPerHost_ = val;
    return *this;
}

Client::Client()
    : reactor_(Aio::Reactor::create())
    , ioIndex(0)
{
}

Client::~Client() {
    for (auto& queues: requestsQueues) {
        auto& q = queues.second;
        for (;;) {
            Connection::RequestData* d;
            if (!q.dequeue(d)) break;

            delete d;
        }
    }
}

Client::Options
Client::options() {
    return Client::Options();
}

void
Client::init(const Client::Options& options) {
    pool.init(options.maxConnectionsPerHost_);
    transport_.reset(new Transport);
    reactor_->init(Aio::AsyncContext(options.threads_));
    transportKey = reactor_->addHandler(transport_);
    reactor_->run();
}

void
Client::shutdown() {
    reactor_->shutdown();
}

RequestBuilder
Client::get(std::string resource)
{
    return prepareRequest(std::move(resource), Http::Method::Get);
}

RequestBuilder
Client::post(std::string resource)
{
    return prepareRequest(std::move(resource), Http::Method::Post);
}

RequestBuilder
Client::put(std::string resource)
{
    return prepareRequest(std::move(resource), Http::Method::Put);
}

RequestBuilder
Client::patch(std::string resource)
{
    return prepareRequest(std::move(resource), Http::Method::Patch);
}

RequestBuilder
Client::del(std::string resource)
{
    return prepareRequest(std::move(resource), Http::Method::Delete);
}

RequestBuilder
Client::prepareRequest(std::string resource, Http::Method method)
{
    RequestBuilder builder(this);
    builder
        .resource(std::move(resource))
        .method(method);

    return builder;
}

Async::Promise<Response>
Client::doRequest(
        Http::Request request,
        std::chrono::milliseconds timeout)
{
    request.headers_.remove<Header::UserAgent>();
    auto resource = request.resource();

    auto s = splitUrl(resource);
    auto conn = pool.pickConnection(s.getHost());

    if (conn == nullptr) {
        return Async::Promise<Response>([=](Async::Resolver& resolve, Async::Rejection& reject) {
            Guard guard(queuesLock);

            std::unique_ptr<Connection::RequestData> data(
                    new Connection::RequestData(std::move(resolve), std::move(reject), request, timeout, nullptr));

            auto& queue = requestsQueues[s.getHost()];
            if (!queue.enqueue(data.get()))
                data->reject(std::runtime_error("Queue is full"));
            else
                data.release();
        });
    }
    else {

        if (!conn->hasTransport()) {
            auto transports = reactor_->handlers(transportKey);
            auto index = ioIndex.fetch_add(1) % transports.size();

            auto transport = std::static_pointer_cast<Transport>(transports[index]);
            conn->associateTransport(transport);

        }

        if (!conn->isConnected()) {
            conn->connect(*reactor_, transportKey, s);
        }

        return conn->perform(request, timeout, [=]() {
            pool.releaseConnection(conn);
            processRequestQueue();
        });
    }
}

void
Client::processRequestQueue() {
    Guard guard(queuesLock);

    for (auto& queues: requestsQueues) {
        const auto& domain = queues.first;
        auto& queue = queues.second;

        for (;;) {
            auto conn = pool.pickConnection(domain);
            if (!conn)
                break;

            auto& queue = queues.second;
            Connection::RequestData *data;
            if (!queue.dequeue(data)) {
                pool.releaseConnection(conn);
                break;
            }

            conn->performImpl(
                    data->request,
                    data->timeout,
                    std::move(data->resolve), std::move(data->reject),
                    [=]() {
                        pool.releaseConnection(conn);
                        processRequestQueue();
                    });

            delete data;
        }
    }
}

} // namespace Http
} // namespace Pistache
