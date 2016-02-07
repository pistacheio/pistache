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

static constexpr const char* UA = "pistache/0.1";

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
            handleIncoming(fd);
        }
        else if (entry.isWritable()) {
            auto tag = entry.getTag();
            auto fd = tag.value();

            auto connIt = pendingConnections.find(fd);
            if (connIt != std::end(pendingConnections)) {
                auto& conn = connIt->second;
                conn.resolve();
                pendingConnections.erase(fd);
                continue; 
            }

#if 0
            auto writeIt = toWrite.find(fd);
            if (writeIt != std::end(toWrite)) {
                /* @Bug: should not need modifyFd, investigate why I can't use
                 * registerFd
                 */
                io()->modifyFd(fd, NotifyOn::Read, Polling::Mode::Edge);

                auto& write = writeIt->second;
                asyncWriteImpl(fd, write, Retry);
                continue;
            }
#endif

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
Transport::asyncConnect(Fd fd, const struct sockaddr* address, socklen_t addr_len)
{
    return Async::Promise<void>([=](Async::Resolver& resolve, Async::Rejection& reject) {
        PendingConnection conn(std::move(resolve), std::move(reject), fd, address, addr_len);
        auto *entry = connectionsQueue.allocEntry(std::move(conn));
        connectionsQueue.push(entry);
    });
}

void
Transport::asyncSendRequest(
        Fd fd,
        const Buffer& buffer,
        Async::Resolver resolve,
        Async::Rejection reject,
        OnResponseParsed onParsed) {

    if (std::this_thread::get_id() != io()->thread()) {
        InflightRequest req(std::move(resolve), std::move(reject), fd, buffer.detach(), std::move(onParsed));
        auto detached = buffer.detach();
        auto *e = requestsQueue.allocEntry(std::move(req));
        requestsQueue.push(e);
    } else {
        InflightRequest req(std::move(resolve), std::move(reject), fd, buffer, std::move(onParsed));

        asyncSendRequestImpl(req);
    }
}


void
Transport::asyncSendRequestImpl(
        InflightRequest& req, WriteStatus status)
{
    auto buffer = req.buffer;

    auto cleanUp = [&]() {
        if (buffer.isOwned) delete[] buffer.data;
    };

    auto fd = req.fd;

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
                io()->modifyFd(fd, NotifyOn::Read | NotifyOn::Write, Polling::Mode::Edge);
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
                auto& queue = inflightRequests[fd];
                queue.push_back(std::move(req));
                break;
            }
        }
    }
}

void
Transport::handleRequestsQueue() {
    // Let's drain the queue
    for (;;) {
        std::unique_ptr<PollableQueue<InflightRequest>::Entry> entry(requestsQueue.pop());
        if (!entry) break;

        auto &req = entry->data();
        asyncSendRequestImpl(req);
    }
}

void
Transport::handleConnectionQueue() {
    for (;;) {
        std::unique_ptr<PollableQueue<PendingConnection>::Entry> entry(connectionsQueue.pop());
        if (!entry) break;

        auto &conn = entry->data();
        int res = ::connect(conn.fd, conn.addr, conn.addr_len);
        if (res == -1) {
            if (errno == EINPROGRESS) {
                io()->registerFdOneShot(conn.fd, NotifyOn::Write);
                pendingConnections.insert(
                        std::make_pair(conn.fd, std::move(conn)));
            }
            else {
                conn.reject(Error::system("Failed to connect"));
            }
        }
    }
}

void
Transport::handleIncoming(Fd fd) {
    char buffer[Const::MaxBuffer];
    memset(buffer, 0, sizeof buffer);

    ssize_t totalBytes = 0;
    for (;;) {

        ssize_t bytes;

        bytes = recv(fd, buffer + totalBytes, Const::MaxBuffer - totalBytes, 0);
        if (bytes == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (totalBytes > 0) {
                    handleResponsePacket(fd, buffer, totalBytes);
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
Transport::handleResponsePacket(Fd fd, const char* buffer, size_t totalBytes) {
    auto it = inflightRequests.find(fd);
    if (it == std::end(inflightRequests))
        throw std::runtime_error("Received response for a non-inflight request");

    auto &queue = it->second;
    auto &req = queue.front();
    req.feed(buffer, totalBytes);
    if (req.parser->parse() == Private::State::Done) {
        req.resolve(std::move(req.parser->response));
        queue.pop_front();
    }
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

        transport_->asyncConnect(sfd, addr->ai_addr, addr->ai_addrlen)
            .then([=]() { 
                connectionState_ = Connected;
                fd = sfd;
                transport_->io()->modifyFd(fd, NotifyOn::Read);
                processRequestQueue();
            }, Async::Throw);
        break;

    }

    if (sfd < 0)
        throw std::runtime_error("Failed to connect");
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

Async::Promise<Response>
Connection::perform(
        const Http::Request& request,
        std::string host,
        OnDone onDone) {
    return Async::Promise<Response>([=](Async::Resolver& resolve, Async::Rejection& reject) {
        if (!isConnected()) {
        auto* entry = requestsQueue.allocEntry(
                RequestData(std::move(resolve), std::move(reject), request, std::move(host), std::move(onDone)));
        requestsQueue.push(entry);
        } else {
            performImpl(request, std::move(host), std::move(resolve), std::move(reject), std::move(onDone));
        }
    });
}

#if 0
struct OnRequestWriteCompleted {
    OnRequestWriteCompleted(
            const std::shared_ptr<Transport>& transport,
            Fd fd,
            Async::Resolver resolve, Async::Rejection reject,
            Connection::OnDone onDone)
        : transport(transport)
        , fd(fd)
        , resolve(std::move(resolve))
        , reject(std::move(reject))
        , onDone(std::move(onDone))
    { }

    void operator()(size_t bytes) {
        transport->addInFlight(fd, std::move(resolve), std::move(reject));
        onDone();
    }

    std::shared_ptr<Transport> transport;
    Fd fd;
    Async::Resolver resolve;
    Async::Rejection reject;
    Connection::OnDone onDone;
};
#endif

void
Connection::performImpl(
        const Http::Request& request,
        std::string host,
        Async::Resolver resolve,
        Async::Rejection reject,
        OnDone onDone) {

    DynamicStreamBuf buf(128);

    if (!writeRequest(request, std::move(host), buf))
        reject(std::runtime_error("Could not write request"));

    auto buffer = buf.buffer();
#if 0
    OnRequestWriteCompleted onCompleted(transport_, fd, std::move(resolve), std::move(reject), std::move(onDone));
#endif
    transport_->asyncSendRequest(fd, buffer, std::move(resolve), std::move(reject), std::move(onDone));
}

void
Connection::processRequestQueue() {
    for (;;) {
        std::unique_ptr<Queue<RequestData>::Entry> entry(requestsQueue.pop());
        if (!entry) break;

        auto &req = entry->data();
        performImpl(req.request,
                std::move(req.host), std::move(req.resolve), std::move(req.reject), std::move(req.onDone));
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

Client::Options&
Client::Options::threads(int val) {
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
Client::request(std::string val) {
    RequestBuilder builder;
    builder.resource(std::move(val));
    return builder;
}

Async::Promise<Http::Response>
Client::get(const Http::Request& request, std::chrono::milliseconds timeout)
{
    return doRequest(request, Http::Method::Get, timeout);
}

Async::Promise<Http::Response>
Client::post(const Http::Request& request, std::chrono::milliseconds timeout)
{
    return doRequest(request, Http::Method::Post, timeout);
}

Async::Promise<Http::Response>
Client::put(const Http::Request& request, std::chrono::milliseconds timeout)
{
    return doRequest(request, Http::Method::Put, timeout);
}

Async::Promise<Http::Response>
Client::del(const Http::Request& request, std::chrono::milliseconds timeout)
{
    return doRequest(request, Http::Method::Delete, timeout);
}

Async::Promise<Response>
Client::doRequest(
        const Http::Request& request,
        Http::Method method,
        std::chrono::milliseconds timeout)
{
    unsafe {
        auto &req = const_cast<Http::Request &>(request);
        req.method_ = method;
        req.headers_.remove<Header::UserAgent>();
    }

    auto conn = pool.pickConnection();
    if (conn == nullptr) {
        return Async::Promise<Response>([=](Async::Resolver& resolve, Async::Rejection& reject) {
            auto entry = requestsQueue.allocEntry(
                    Connection::RequestData(std::move(resolve), std::move(reject), request, host_, nullptr));
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

        return conn->perform(request, host_, [=]() {
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

        std::unique_ptr<Queue<Connection::RequestData>::Entry> entry(requestsQueue.pop());
        if (!entry) {
            pool.releaseConnection(conn);
            break;
        }

        auto& req = entry->data();
        conn->performImpl(
                req.request, std::move(req.host),
                std::move(req.resolve), std::move(req.reject),
                [=]() {
                    pool.releaseConnection(conn);
                    processRequestQueue();
                });
    }
}

} // namespace Http

} // namespace Net
