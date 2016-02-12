/* 
   Mathieu Stefani, 29 janvier 2016
   
   Implementation of the Http client
*/

#include "client.h"
#include "stream.h"
#include <sys/sendfile.h>
#include <netdb.h>

using namespace Polling;

namespace Net {

namespace Http {

namespace Experimental {

static constexpr const char* UA = "pistache/0.1";

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
    typename std::enable_if<Header::IsHeader<H>::value, Stream&>::type
    writeHeader(Stream& stream, Args&& ...args) {
        H header(std::forward<Args>(args)...);

        stream << H::Name << ": ";
        header.write(stream);

        stream << crlf;

        return stream;
    }

    bool writeHeaders(const Header::Collection& headers, DynamicStreamBuf& buf) {
        std::ostream os(&buf);

        for (const auto& header: headers.list()) {
            OUT(os << header->name() << ": ");
            OUT(header->write(os));
            OUT(os << crlf);
        }

        return true;
    }

    bool writeCookies(const CookieJar& cookies, DynamicStreamBuf& buf) {
        std::ostream os(&buf);
        for (const auto& cookie: cookies) {
            OUT(os << "Cookie: ");
            OUT(cookie.write(os));
            OUT(os << crlf);
        }

        return true;

    }

    bool writeRequest(const Http::Request& request, std::string host, DynamicStreamBuf &buf) {
        std::ostream os(&buf);

        OUT(os << request.method() << " ");
        OUT(os << request.resource());
        OUT(os << " HTTP/1.1" << crlf);

        if (!writeCookies(request.cookies(), buf)) return false;
        if (!writeHeaders(request.headers(), buf)) return false;

        if (!writeHeader<Header::UserAgent>(os, UA)) return false;
        if (!writeHeader<Header::Host>(os, std::move(host))) return false;
        OUT(os << crlf);

        return true;


    }

#undef OUT
}

void
Transport::onReady(const Io::FdSet& fds) {
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
        else if (entry.isWritable()) {
            auto tag = entry.getTag();
            auto fd = tag.value();

            auto connIt = connections.find(fd);
            if (connIt != std::end(connections)) {
                auto& conn = connIt->second;
                conn.resolve();
                // We are connected, we can start reading data now
                io()->modifyFd(conn.connection->fd, NotifyOn::Read);
                continue; 
            }

            throw std::runtime_error("Unknown fd");
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
        if (std::this_thread::get_id() != io()->thread()) {
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

    auto fd = conn->fd;

    ssize_t totalWritten = 0;
    for (;;) {
        ssize_t bytesWritten = 0;
        auto len = buffer.len - totalWritten;
        auto ptr = buffer.data + totalWritten;
        bytesWritten = ::send(fd, ptr, len, 0);
        if (bytesWritten < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (status == FirstTry) {
                    throw std::runtime_error("Unimplemented, fix me!");
                }
                io()->modifyFd(fd, NotifyOn::Write, Polling::Mode::Edge);
            }
            else {
                cleanUp();
                req.reject(Net::Error::system("Could not send request"));
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
                    req.timer->registerIo(io());
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
        int res = ::connect(conn->fd, data.addr, data.addr_len);
        if (res == -1) {
            if (errno == EINPROGRESS) {
                io()->registerFdOneShot(conn->fd, NotifyOn::Write);
            }
            else {
                data.reject(Error::system("Failed to connect"));
                continue;
            }
        }
        connections.insert(std::make_pair(conn->fd, std::move(data)));
    }
}

void
Transport::handleIncoming(const std::shared_ptr<Connection>& connection) {
    char buffer[Const::MaxBuffer];
    memset(buffer, 0, sizeof buffer);

    ssize_t totalBytes = 0;
    for (;;) {

        ssize_t bytes;

        bytes = recv(connection->fd, buffer + totalBytes, Const::MaxBuffer - totalBytes, 0);
        if (bytes == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (totalBytes > 0) {
                    handleResponsePacket(connection, buffer, totalBytes);
                }
            } else {
                if (errno == ECONNRESET) {
                }
                else {
                    throw std::runtime_error(strerror(errno));
                }
            }
            break;
        }
        else if (bytes == 0) {
            break;
        }

        else {
            totalBytes += bytes;
            if (totalBytes >= Const::MaxBuffer) {
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
Connection::connect(Net::Address addr)
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

        connectionState_ = Connecting;
        fd = sfd;

        transport_->asyncConnect(shared_from_this(), addr->ai_addr, addr->ai_addrlen)
            .then([=]() { 
                socklen_t len = sizeof(saddr);
                getsockname(sfd, (struct sockaddr *)&saddr, &len);
                connectionState_ = Connected;
                processRequestQueue();
            }, ExceptionPrinter());
        break;

    }

    if (sfd < 0)
        throw std::runtime_error("Failed to connect");
}

std::string
Connection::dump() const {
    std::ostringstream oss;
    oss << "Connection(fd = " << fd << ", src_port = ";
    oss << ntohs(saddr.sin_port) << ")";
    return oss.str();
}

bool
Connection::isConnected() const {
    return connectionState_ == Connected;
}

void
Connection::close() {
    connectionState_ = NotConnected;
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
        auto req = std::move(inflightRequests.front());
        inflightRequests.pop_back();

        if (req.timer) {
            req.timer->disarm();
            timerPool_.releaseTimer(req.timer);
        }

        req.resolve(std::move(parser_.response));
        req.onDone();
    }
}

void
Connection::handleTimeout() {
    auto req = std::move(inflightRequests.front());
    inflightRequests.pop_back();

    timerPool_.releaseTimer(req.timer);
    req.onDone();
    /* @API: create a TimeoutException */
    req.reject(std::runtime_error("Timeout"));
}

Async::Promise<Response>
Connection::perform(
        const Http::Request& request,
        std::string host,
        std::chrono::milliseconds timeout,
        Connection::OnDone onDone) {
    return Async::Promise<Response>([=](Async::Resolver& resolve, Async::Rejection& reject) {
        if (!isConnected()) {
            auto* entry = requestsQueue.allocEntry(
                RequestData(
                    std::move(resolve),
                    std::move(reject),
                    request,
                    std::move(host),
                    timeout,
                    std::move(onDone)));
            requestsQueue.push(entry);
        } else {
            performImpl(request, std::move(host), timeout, std::move(resolve), std::move(reject), std::move(onDone));
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
        std::string host,
        std::chrono::milliseconds timeout,
        Async::Resolver resolve,
        Async::Rejection reject,
        Connection::OnDone onDone) {

    DynamicStreamBuf buf(128);

    if (!writeRequest(request, std::move(host), buf))
        reject(std::runtime_error("Could not write request"));

    auto buffer = buf.buffer();
    std::shared_ptr<TimerPool::Entry> timer(nullptr);
    if (timeout.count() > 0) {
        timer = timerPool_.pickTimer();
        timer->arm(timeout);
    }

    // Move the resolver and rejecter inside the lambda
    auto resolveMover = make_copy_mover(std::move(resolve));
    auto rejectMover = make_copy_mover(std::move(reject));

    /*
     * @Incomplete: currently, if the promise is rejected in asyncSendRequest,
     * it will abort the current execution (NoExcept). Instead, it should reject
     * the original promise from the request. The thing is that we currently can not
     * do that since we transfered the ownership of the original rejecter to the
     * mover so that it will be moved inside the continuation lambda.
     *
     * Since Resolver and Rejection objects are not copyable by default, we could
     * implement a clone() member function so that it's explicit that a copy operation
     * has been originated from the user
     *
     * The reason why Resolver and Rejection copy constructors is disabled is to avoid
     * double-resolve and double-reject of a promise
     */

    transport_->asyncSendRequest(shared_from_this(), timer, buffer).then(
        [=](ssize_t bytes) mutable {
            inflightRequests.push_back(RequestEntry(std::move(resolveMover), std::move(rejectMover), std::move(timer), std::move(onDone)));
        }
   , Async::NoExcept);
}

void
Connection::processRequestQueue() {
    for (;;) {
        auto entry = requestsQueue.popSafe();
        if (!entry) break;

        auto &req = entry->data();
        performImpl(req.request,
                std::move(req.host), req.timeout, std::move(req.resolve), std::move(req.reject), std::move(req.onDone));
    }

}

void
ConnectionPool::init(size_t max)
{
    for (size_t i = 0; i < max; ++i) {
        connections.push_back(std::make_shared<Connection>());
    }

    usedCount.store(0);
}

std::shared_ptr<Connection>
ConnectionPool::pickConnection() {
    for (auto& conn: connections) {
        auto& state = conn->state_;
        auto curState = static_cast<uint32_t>(Connection::State::Idle);
        auto newState = static_cast<uint32_t>(Connection::State::Used);
        if (state.compare_exchange_strong(curState, newState)) {
            usedCount.fetch_add(1);
            return conn;
        }
    }

    return nullptr;
}

void
ConnectionPool::releaseConnection(const std::shared_ptr<Connection>& connection) {
    connection->state_.store(static_cast<uint32_t>(Connection::State::Idle));
    usedCount.fetch_add(-1);
}

size_t
ConnectionPool::availableCount() const {
    return connections.size() - usedCount.load();
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
}

Async::Promise<Response>
RequestBuilder::send() {
    return client_->doRequest(std::move(request_), timeout_);
}

Client::Options&
Client::Options::threads(int val) {
    if (val > 1) {
        throw std::invalid_argument("Multi-threaded client is not yet supported");
    }
    threads_ = val;
    return *this;
}

Client::Options&
Client::Options::maxConnections(int val) {
    maxConnections_ = val;
    return *this;
}

Client::Options&
Client::Options::keepAlive(bool val) {
    keepAlive_ = val;
}

Client::Client(const std::string& base)
    : url_(base)
    , ioIndex(0)
{
    host_ = url_;
    constexpr const char *Http = "http://";
    constexpr size_t Size = sizeof("http://") - 1;
    if (!host_.compare(0, Size, Http))
        host_.erase(0, Size);
}

Client::Options
Client::options() {
    return Client::Options();
}

void
Client::init(const Client::Options& options) {
    transport_.reset(new Transport);
    io_.init(options.threads_, transport_);
    io_.start();

    constexpr const char *Http = "http://";
    constexpr size_t Size = sizeof("http://") - 1;

    std::string url(url_);
    if (!url.compare(0, Size, Http)) {
        url.erase(0, Size);
    }

    auto pos = url.find(':');
    Port port(80);
    std::string host;
    if (pos != std::string::npos) {
        host = url.substr(0, pos);
        port = std::stol(url.substr(pos + 1));
    }
    else {
        host = url;
    }

    addr_ = Net::Address(host, port);

    pool.init(options.maxConnections_);
}

void
Client::shutdown() {
    io_.shutdown();
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

    auto conn = pool.pickConnection();
    if (conn == nullptr) {
        return Async::Promise<Response>([=](Async::Resolver& resolve, Async::Rejection& reject) {
            auto entry = requestsQueue.allocEntry(
                    Connection::RequestData(std::move(resolve), std::move(reject), request, host_, timeout, nullptr));
            requestsQueue.push(entry);
        });
    }
    else {

        if (!conn->hasTransport()) {
            auto index = ioIndex.fetch_add(1) % io_.size();
            auto service = io_.service(index);

            auto transport = std::static_pointer_cast<Transport>(service->handler()); 
            conn->associateTransport(transport);

        }

        if (!conn->isConnected())
            conn->connect(addr_);

        return conn->perform(request, host_, timeout, [=]() {
            pool.releaseConnection(conn);
            processRequestQueue();
        });
    }

}

void
Client::processRequestQueue() {
    for (;;) {
        auto conn = pool.pickConnection();
        if (!conn) break;

        auto entry = requestsQueue.popSafe();
        if (!entry) {
            pool.releaseConnection(conn);
            break;
        }

        auto& req = entry->data();
        conn->performImpl(
                req.request, std::move(req.host),
                req.timeout,
                std::move(req.resolve), std::move(req.reject),
                [=]() {
                    pool.releaseConnection(conn);
                    processRequestQueue();
                });
    }
}

} // namespace Experimental

} // namespace Http

} // namespace Net
