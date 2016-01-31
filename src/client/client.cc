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

    bool writeRequest(const Http::Request& request, DynamicStreamBuf &buf) {
        std::ostream os(&buf);

        OUT(os << "GET ");
        OUT(os << request.resource());
        OUT(os << " HTTP/1.1" << crlf);

        if (!writeCookies(request.cookies(), buf)) return false;
        if (!writeHeaders(request.headers(), buf)) return false;

        if (!writeHeader<Header::UserAgent>(os, "restpp/0.1")) return false;
        if (!writeHeader<Header::Host>(os, "foaas.com")) return false;
        OUT(os << crlf);

        return true;


    }

#undef OUT
}

Transport::ConnectionEvent::ConnectionEvent(const Connection* connection)
    : connection_(connection)
{ }

void
Transport::onReady(const Io::FdSet& fds) {
    for (const auto& entry: fds) {
        if (entry.getTag() == connectionsQueue.tag()) {
            handleConnectionQueue();
        }
        else if (entry.getTag() == writesQueue.tag()) {
            handleWriteQueue();
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

            auto writeIt = toWrite.find(fd);
            if (writeIt != std::end(toWrite)) {
                io()->modifyFd(fd, NotifyOn::Read, Polling::Mode::Edge);

                auto& write = writeIt->second;
                asyncWriteImpl(fd, write, Retry);
                continue;
            }

            throw std::runtime_error("Unknown fd");
        }
    }
}

void
Transport::registerPoller(Polling::Epoll& poller) {
    writesQueue.bind(poller);
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
Transport::asyncWriteImpl(Fd fd, Transport::OnHoldWrite& entry, WriteStatus status) {
    asyncWriteImpl(fd, entry.flags, entry.buffer, std::move(entry.resolve), std::move(entry.reject), status);
}

void
Transport::asyncWriteImpl(
        Fd fd, int flags, const BufferHolder& buffer,
        Async::Resolver resolve, Async::Rejection reject, WriteStatus status)
{
    auto cleanUp = [&]() {
        if (buffer.isRaw()) {
            auto raw = buffer.raw();
            if (raw.isOwned) delete[] raw.data;
        }

        if (status == Retry)
            toWrite.erase(fd);
    };

    ssize_t totalWritten = 0;
    for (;;) {
        ssize_t bytesWritten = 0;
        auto len = buffer.size() - totalWritten;
        if (buffer.isRaw()) {
            auto raw = buffer.raw();
            auto ptr = raw.data + totalWritten;
            bytesWritten = ::send(fd, ptr, len, flags);
        } else {
            auto file = buffer.fd();
            off_t offset = totalWritten;
            bytesWritten = ::sendfile(fd, file, &offset, len);
        }
        if (bytesWritten < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (status == FirstTry) {
                    toWrite.insert(
                            std::make_pair(fd,
                                OnHoldWrite(std::move(resolve), std::move(reject), buffer.detach(totalWritten), flags)));
                }
                io()->modifyFd(fd, NotifyOn::Read | NotifyOn::Write, Polling::Mode::Edge);
            }
            else {
                cleanUp();
                reject(Net::Error::system("Could not write data"));
            }
            break;
        }
        else {
            totalWritten += bytesWritten;
            if (totalWritten == len) {
                cleanUp();
                resolve(totalWritten);
                break;
            }
        }
    }
}

void
Transport::handleWriteQueue() {
    // Let's drain the queue
    for (;;) {
        std::unique_ptr<PollableQueue<OnHoldWrite>::Entry> entry(writesQueue.pop());
        if (!entry) break;

        auto &write = entry->data();
        asyncWriteImpl(write.peerFd, write);
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

    auto &req = it->second;
    req.parser->feed(buffer, totalBytes);
    if (req.parser->parse() == Private::State::Done) {
        req.resolve(std::move(req.parser->response));
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

        state_.store(static_cast<uint32_t>(State::Connecting));

        transport_->asyncConnect(sfd, addr->ai_addr, addr->ai_addrlen)
            .then([=]() { 
                fd = sfd;
                transport_->io()->modifyFd(fd, NotifyOn::Read);
                state_.store(static_cast<uint32_t>(State::Connected));
                processRequestQueue();
            }, Async::Throw);
        break;

    }

    if (sfd < 0)
        throw std::runtime_error("Failed to connect");
}

bool
Connection::isConnected() const {
    return static_cast<State>(state_.load()) == State::Connected;
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
Connection::perform(const Http::Request& request) {
    return Async::Promise<Response>([=](Async::Resolver& resolve, Async::Rejection& reject) {
        if (!isConnected()) {
        auto* entry = requestsQueue.allocEntry(RequestData(std::move(resolve), std::move(reject), request));
        requestsQueue.push(entry);
        } else {
            performImpl(request, std::move(resolve), std::move(reject));
        }
    });
}

struct OnRequestWriteCompleted {
    OnRequestWriteCompleted(const std::shared_ptr<Transport>& transport, Fd fd, Async::Resolver resolve, Async::Rejection reject)
        : transport(transport)
        , fd(fd)
        , resolve(std::move(resolve))
        , reject(std::move(reject))
    { }

    void operator()(size_t bytes) const {
        transport->addInFlight(fd, std::move(resolve), std::move(reject));
    }

    mutable std::shared_ptr<Transport> transport;
    Fd fd;
    mutable Async::Resolver resolve;
    mutable Async::Rejection reject;
};

void
Connection::performImpl(
        const Http::Request& request,
        Async::Resolver resolve,
        Async::Rejection reject) {

    DynamicStreamBuf buf(128);

    if (!writeRequest(request, buf))
        reject(std::runtime_error("Could not write request"));

    auto buffer = buf.buffer();
    OnRequestWriteCompleted onCompleted(transport_, fd, std::move(resolve), std::move(reject));
    transport_->asyncWrite(fd, buffer).then(std::move(onCompleted) , Async::Throw);
}

void
Connection::processRequestQueue() {
    for (;;) {
        std::unique_ptr<Queue<RequestData>::Entry> entry(requestsQueue.pop());
        if (!entry) break;

        auto &req = entry->data();
        performImpl(req.request, std::move(req.resolve), std::move(req.reject));
    }

}

void
ConnectionPool::init(size_t max)
{
    for (size_t i = 0; i < max; ++i) {
        connections.push_back(std::make_shared<Connection>());
    }
}

std::shared_ptr<Connection>
ConnectionPool::pickConnection() {
    for (auto& conn: connections) {
        auto& state = conn->state_;
        if (static_cast<Connection::State>(state.load()) == Connection::State::Idle) {
            auto curState = static_cast<uint32_t>(Connection::State::Idle);
            auto newState = Connection::State::Used;
            if (state.compare_exchange_strong(
                curState,
                static_cast<uint32_t>(Connection::State::Used))) {
                return conn;
            }
        }
    }

    return nullptr;
}

void
ConnectionPool::returnConnection(const std::shared_ptr<Connection>& connection) {
    connection->state_.store(static_cast<uint32_t>(Connection::State::Idle));
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
{ }

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

RequestBuilder
Client::newRequest(std::string val) {
    RequestBuilder builder;
    builder.resource(std::move(val));
    return builder;
}

Async::Promise<Http::Response>
Client::get(const Http::Request& request, std::chrono::milliseconds timeout)
{
    auto conn = pool.pickConnection();
    if (conn == nullptr) {
        std::cout << "No connection available yet, bailing-out" << std::endl;
    }
    else {

        if (!conn->hasTransport()) {
            auto index = ioIndex.fetch_add(1) % io_.size();
            auto service = io_.service(index);

            auto transport = std::static_pointer_cast<Transport>(service->handler()); 
            conn->associateTransport(transport);

            conn->connect(addr_);
        }

        return conn->perform(request);
    }

}

} // namespace Http

} // namespace Net
