/* http.cc
   Mathieu Stefani, 13 August 2015

   Http layer implementation
*/

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <ctime>
#include <iomanip>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <pistache/common.h>
#include <pistache/http.h>
#include <pistache/net.h>
#include <pistache/peer.h>
#include <pistache/transport.h>

using namespace std;

namespace Pistache {
namespace Http {

template<typename H, typename Stream, typename... Args>
typename std::enable_if<Header::IsHeader<H>::value, Stream&>::type
writeHeader(Stream& stream, Args&& ...args) {
    H header(std::forward<Args>(args)...);

    stream << H::Name << ": ";
    header.write(stream);

    stream << crlf;

    return stream;
}

namespace {
    bool writeStatusLine(Version version, Code code, DynamicStreamBuf& buf) {
        #define OUT(...) \
            do { \
                __VA_ARGS__; \
                if (!os) return false; \
            } while (0)

        std::ostream os(&buf);

        OUT(os << version << " ");
        OUT(os << static_cast<int>(code));
        OUT(os << ' ');
        OUT(os << code);
        OUT(os << crlf);

        return true;

        #undef OUT
    }

    bool writeHeaders(const Header::Collection& headers, DynamicStreamBuf& buf) {
        #define OUT(...) \
            do { \
                __VA_ARGS__; \
                if (!os)  return false; \
            } while (0)

        std::ostream os(&buf);

        for (const auto& header: headers.list()) {
            OUT(os << header->name() << ": ");
            OUT(header->write(os));
            OUT(os << crlf);
        }

        return true;

        #undef OUT
    }

    bool writeCookies(const CookieJar& cookies, DynamicStreamBuf& buf) {
        #define OUT(...) \
            do { \
                __VA_ARGS__; \
                if (!os) return false; \
            } while (0)

        std::ostream os(&buf);
        for (const auto& cookie: cookies) {
            OUT(os << "Set-Cookie: ");
            OUT(cookie.write(os));
            OUT(os << crlf);
        }

        return true;

        #undef OUT
    }

}

static constexpr const char* ParserData = "__Parser";

namespace Private {

    void
    Step::raise(const char* msg, Code code /* = Code::Bad_Request */) {
        throw HttpError(code, msg);
    }

    State
    RequestLineStep::apply(StreamCursor& cursor) {
        StreamCursor::Revert revert(cursor);

        // Method
        //
        struct MethodValue {
            const char* const str;
            const size_t len;

            Method repr;
        };

        static constexpr MethodValue Methods[] = {
        #define METHOD(repr, str) \
            { str, sizeof(str) - 1, Method::repr },
            HTTP_METHODS
        #undef METHOD
        };

        auto request = static_cast<Request *>(message);

        bool found = false;
        for (const auto& method: Methods) {
            if (match_raw(method.str, method.len, cursor)) {
                request->method_ = method.repr;
                found = true;
                break;
            }
        }

        if (!found) {
            raise("Unknown HTTP request method");
        }

        int n;

        if (cursor.eof()) return State::Again;
        else if ((n = cursor.current()) != ' ')
            raise("Malformed HTTP request after Method, expected SP");

        if (!cursor.advance(1)) return State::Again;

        StreamCursor::Token resToken(cursor);
        while ((n = cursor.current()) != '?' && n != ' ')
            if (!cursor.advance(1)) return State::Again;

        request->resource_ = resToken.text();

        // Query parameters of the Uri
        if (n == '?') {
            if (!cursor.advance(1)) return State::Again;

            while ((n = cursor.current()) != ' ') {
                StreamCursor::Token keyToken(cursor);
                if (!match_until({ '=', ' ', '&' }, cursor))
                    return State::Again;

                std::string key = keyToken.text();

                auto c = cursor.current();
                if (c == ' ') {
                    request->query_.add(std::move(key), "");
                } else if (c == '&') {
                    request->query_.add(std::move(key), "");
                    if (!cursor.advance(1)) return State::Again;
                }
                else if (c == '=') {
                    if (!cursor.advance(1)) return State::Again;

                    StreamCursor::Token valueToken(cursor);
                    if (!match_until({ ' ', '&' }, cursor))
                        return State::Again;

                    std::string value = valueToken.text();
                    request->query_.add(std::move(key), std::move(value));
                    if (cursor.current() == '&') {
                        if (!cursor.advance(1)) return State::Again;
                    }
                }
            }
        }

        // @Todo: Fragment

        // SP
        if (!cursor.advance(1)) return State::Again;

        // HTTP-Version
        StreamCursor::Token versionToken(cursor);

        while (!cursor.eol())
            if (!cursor.advance(1)) return State::Again;

        const char* ver = versionToken.rawText();
        const size_t size = versionToken.size();
        if (strncmp(ver, "HTTP/1.0", size) == 0) {
            request->version_ = Version::Http10;
        }
        else if (strncmp(ver, "HTTP/1.1", size) == 0) {
            request->version_ = Version::Http11;
        }
        else {
            raise("Encountered invalid HTTP version");
        }

        if (!cursor.advance(2)) return State::Again;

        revert.ignore();
        return State::Next;

    }

    State
    ResponseLineStep::apply(StreamCursor& cursor) {
        StreamCursor::Revert revert(cursor);

        auto *response = static_cast<Response *>(message);

        if (match_raw("HTTP/1.1", sizeof("HTTP/1.1") - 1, cursor)) {
            //response->version = Version::Http11;
        }
        else if (match_raw("HTTP/1.0", sizeof("HTTP/1.0") - 1, cursor)) {
        }
        else {
            raise("Encountered invalid HTTP version");
        }

        int n;
        // SP
        if ((n = cursor.current()) != StreamCursor::Eof && n != ' ')
            raise("Expected SPACE after http version");
        if (!cursor.advance(1)) return State::Again;

        StreamCursor::Token codeToken(cursor);
        if (!match_until(' ', cursor))
            return State::Again;

        char *end;
        auto code = strtol(codeToken.rawText(), &end, 10);
        if (*end != ' ')
            raise("Failed to parsed return code");
        response->code_ = static_cast<Http::Code>(code);

        if (!cursor.advance(1)) return State::Again;

        while (!cursor.eol()) cursor.advance(1);

        if (!cursor.advance(2)) return State::Again;

        revert.ignore();
        return State::Next;

    }

    State
    HeadersStep::apply(StreamCursor& cursor) {
        StreamCursor::Revert revert(cursor);

        while (!cursor.eol()) {
            StreamCursor::Revert headerRevert(cursor);

            // Read the header name
            size_t start = cursor;

            while (cursor.current() != ':')
                if (!cursor.advance(1)) return State::Again;

            // Skip the ':'
            if (!cursor.advance(1)) return State::Again;

            std::string name = std::string(cursor.offset(start), cursor.diff(start) - 1);

            // Ignore spaces
            while (cursor.current() == ' ')
                if (!cursor.advance(1)) return State::Again;

            // Read the header value
            start = cursor;
            while (!cursor.eol()) {
                if (!cursor.advance(1)) return State::Again;
            }

            if (name == "Cookie") {
                message->cookies_.addFromRaw(cursor.offset(start), cursor.diff(start));
            }

            else if (Header::Registry::isRegistered(name)) {
                std::shared_ptr<Header::Header> header = Header::Registry::makeHeader(name);
                header->parseRaw(cursor.offset(start), cursor.diff(start));
                message->headers_.add(header);
            }
            else {
                std::string value(cursor.offset(start), cursor.diff(start));
                message->headers_.addRaw(Header::Raw(std::move(name), std::move(value)));
            }

            // CRLF
            if (!cursor.advance(2)) return State::Again;

            headerRevert.ignore();
        }

        if (!cursor.advance(2)) return State::Again;

        revert.ignore();
        return State::Next;
    }

    State
    BodyStep::apply(StreamCursor& cursor) {
        auto cl = message->headers_.tryGet<Header::ContentLength>();
        auto te = message->headers_.tryGet<Header::TransferEncoding>();

        if (cl && te)
            raise("Got mutually exclusive ContentLength and TransferEncoding header");

        if (cl)
            return parseContentLength(cursor, cl);

        if (te)
            return parseTransferEncoding(cursor, te);

        return State::Done;

    }

    State
    BodyStep::parseContentLength(StreamCursor& cursor, const std::shared_ptr<Header::ContentLength>& cl) {
        auto contentLength = cl->value();

        auto readBody = [&](size_t size) {
            StreamCursor::Token token(cursor);
            const size_t available = cursor.remaining();

            // We have an incomplete body, read what we can
            if (available < size) {
                cursor.advance(available);
                message->body_.append(token.rawText(), token.size());

                bytesRead += available;

                return false;
            }

            cursor.advance(size);
            message->body_.append(token.rawText(), token.size());
            return true;
        };

        // We already started to read some bytes but we got an incomplete payload
        if (bytesRead > 0) {
            // How many bytes do we still need to read ?
            const size_t remaining = contentLength - bytesRead;
            if (!readBody(remaining)) return State::Again;
        }
        // This is the first time we are reading the payload
        else {
            message->body_.reserve(contentLength);
            if (!readBody(contentLength)) return State::Again;
        }

        bytesRead = 0;
        return State::Done;
    }

    BodyStep::Chunk::Result
    BodyStep::Chunk::parse(StreamCursor& cursor) {
        if (size == -1) {
            StreamCursor::Revert revert(cursor);
            StreamCursor::Token chunkSize(cursor);

            while (!cursor.eol()) if (!cursor.advance(1)) return Incomplete;

            char *end;
            const char *raw = chunkSize.rawText();
            auto sz = std::strtol(raw, &end, 16);
            if (*end != '\r') throw std::runtime_error("Invalid chunk size");

            // CRLF
            if (!cursor.advance(2)) return Incomplete;

            revert.ignore();

            size = sz;
        }

        if (size == 0)
            return Final;

        message->body_.reserve(size);
        StreamCursor::Token chunkData(cursor);
        const ssize_t available = cursor.remaining();

        if (available < size) {
            cursor.advance(available);
            message->body_.append(chunkData.rawText(), available);
            return Incomplete;
        }
        cursor.advance(size);

        if (!cursor.advance(2)) return Incomplete;

        message->body_.append(chunkData.rawText(), size);
        return Complete;
    }

    State
    BodyStep::parseTransferEncoding(StreamCursor& cursor, const std::shared_ptr<Header::TransferEncoding>& te) {
        auto encoding = te->encoding();
        if (encoding == Http::Header::Encoding::Chunked) {
            Chunk::Result result;
            try {
                while ((result = chunk.parse(cursor)) != Chunk::Final) {
                    if (result == Chunk::Incomplete) return State::Again;

                    chunk.reset();
                    if (cursor.eof()) return State::Again;
                }
            } catch (const std::exception& e) {
                raise(e.what());
            }

            return State::Done;
        }
        else {
            raise("Unsupported Transfer-Encoding", Code::Not_Implemented);
        }
        return State::Done;
    }

    State
    ParserBase::parse() {
        State state = State::Again;
        do {
            Step *step = allSteps[currentStep].get();
            state = step->apply(cursor);
            if (state == State::Next) {
                ++currentStep;
            }
        } while (state == State::Next);

        // Should be either Again or Done
        return state;
    }

    bool
    ParserBase::feed(const char* data, size_t len) {
        return buffer.feed(data, len);
    }

    void
    ParserBase::reset() {
        buffer.reset();
        cursor.reset();

        currentStep = 0;

    }

} // namespace Private

Message::Message()
    : version_(Version::Http11)
{ }

namespace Uri {

    Query::Query()
    { }

    Query::Query(std::initializer_list<std::pair<const std::string, std::string>> params)
        : params(params)
    { }

    void
    Query::add(std::string name, std::string value) {
        params.insert(std::make_pair(std::move(name), std::move(value)));
    }

    Optional<std::string>
    Query::get(const std::string& name) const {
        auto it = params.find(name);
        if (it == std::end(params))
            return None();

        return Some(it->second);
    }

    std::string
    Query::as_str() const {
        std::string query_url;
        for(const auto &e : params) {
            query_url += "&" + e.first + "=" + e.second;
        }
        if(not query_url.empty()) {
            query_url[0] = '?'; // replace first `&` with `?`
        } else {/* query_url is empty */}
        return query_url;
    }

    bool
    Query::has(const std::string& name) const {
        return params.find(name) != std::end(params);
    }

} // namespace Uri

Request::Request()
    : Message()
{ }

Version
Request::version() const {
    return version_;
}

Method
Request::method() const {
    return method_;
}

std::string
Request::resource() const {
    return resource_;
}

std::string
Request::body() const {
    return body_;
}

const Header::Collection&
Request::headers() const {
    return headers_;
}

const Uri::Query&
Request::query() const {
    return query_;
}

const CookieJar&
Request::cookies() const {
    return cookies_;
}

#ifdef LIBSTDCPP_SMARTPTR_LOCK_FIXME
std::shared_ptr<Tcp::Peer>
Request::peer() const {
    auto p = peer_.lock();

    if (!p) throw std::runtime_error("Failed to retrieve peer: Broken pipe");

    return p;
}
#endif


ResponseStream::ResponseStream(
        Message&& other,
        std::weak_ptr<Tcp::Peer> peer,
        Tcp::Transport* transport,
        Timeout timeout,
        size_t streamSize)
    : Message(std::move(other))
    , peer_(std::move(peer))
    , buf_(streamSize)
    , transport_(transport)
    , timeout_(std::move(timeout))
{
    if (!writeStatusLine(version_, code_, buf_))
        throw Error("Response exceeded buffer size");

    if (!writeCookies(cookies_, buf_)) {
        throw Error("Response exceeded buffer size");
    }

    if (writeHeaders(headers_, buf_)) {
        std::ostream os(&buf_);
        /* @Todo @Major:
         * Correctly handle non-keep alive requests
         * Do not put Keep-Alive if version == Http::11 and request.keepAlive == true
        */
        writeHeader<Header::Connection>(os, ConnectionControl::KeepAlive);
        if (!os) throw Error("Response exceeded buffer size");
        writeHeader<Header::TransferEncoding>(os, Header::Encoding::Chunked);
        if (!os) throw Error("Response exceeded buffer size");
        os << crlf;
    }
}

void
ResponseStream::flush() {
    timeout_.disarm();
    auto buf = buf_.buffer();

    auto fd = peer()->fd();
    transport_->asyncWrite(fd, buf);

    buf_.clear();
}

void
ResponseStream::ends() {
    std::ostream os(&buf_);
    os << "0" << crlf;
    os << crlf;

    if (!os) {
        throw Error("Response exceeded buffer size");
    }

    flush();
}

Async::Promise<ssize_t>
ResponseWriter::putOnWire(const char* data, size_t len)
{
    try {
        std::ostream os(&buf_);

#define OUT(...) \
        do { \
            __VA_ARGS__; \
            if (!os) { \
                return Async::Promise<ssize_t>::rejected(Error("Response exceeded buffer size")); \
            } \
        } while (0);

        OUT(writeStatusLine(version_, code_, buf_));
        OUT(writeHeaders(headers_, buf_));
        OUT(writeCookies(cookies_, buf_));

        /* @Todo @Major:
         * Correctly handle non-keep alive requests
         * Do not put Keep-Alive if version == Http::11 and request.keepAlive == true
        */
        OUT(writeHeader<Header::Connection>(os, ConnectionControl::KeepAlive));
        OUT(writeHeader<Header::ContentLength>(os, len));

        OUT(os << crlf);

        if (len > 0) {
            OUT(os.write(data, len));
        }

        auto buffer = buf_.buffer();

        timeout_.disarm();

#undef OUT

        auto fd = peer()->fd();
        return transport_->asyncWrite(fd, buffer);

    } catch (const std::runtime_error& e) {
        return Async::Promise<ssize_t>::rejected(e);
    }
}

Async::Promise<ssize_t>
serveFile(ResponseWriter& response, const char* fileName, const Mime::MediaType& contentType)
{
    struct stat sb;

    int fd = open(fileName, O_RDONLY);
    if (fd == -1) {
        std::string str_error(strerror(errno));
        if(errno == ENOENT) {
            throw HttpError(Http::Code::Not_Found, std::move(str_error));
        }
        //eles if TODO
        /* @Improvement: maybe could we check for errno here and emit a different error
            message
        */
        else {
            throw HttpError(Http::Code::Internal_Server_Error, std::move(str_error));
        }
    }

    int res = ::fstat(fd, &sb);
    close(fd); // Done with fd, close before error can be thrown
    if (res == -1) {
        throw HttpError(Code::Internal_Server_Error, "");
    }

    auto *buf = response.rdbuf();

    std::ostream os(buf);

    #define OUT(...) \
        do { \
            __VA_ARGS__; \
            if (!os) { \
                return Async::Promise<ssize_t>::rejected(Error("Response exceeded buffer size")); \
            } \
        } while (0);

    auto setContentType = [&](const Mime::MediaType& contentType) {
        auto& headers = response.headers();
        auto ct = headers.tryGet<Header::ContentType>();
        if (ct)
            ct->setMime(contentType);
        else
            headers.add<Header::ContentType>(contentType);
    };

    OUT(writeStatusLine(response.version(), Http::Code::Ok, *buf));
    if (contentType.isValid()) {
        setContentType(contentType);
    } else {
        auto mime = Mime::MediaType::fromFile(fileName);
        if (mime.isValid())
            setContentType(mime);
    }

    OUT(writeHeaders(response.headers(), *buf));

    const size_t len = sb.st_size;

    OUT(writeHeader<Header::ContentLength>(os, len));

    OUT(os << crlf);

    auto *transport = response.transport_;
    auto peer = response.peer();
    auto sockFd = peer->fd();

    auto buffer = buf->buffer();
    return transport->asyncWrite(sockFd, buffer, MSG_MORE).then([=](ssize_t) {
        return transport->asyncWrite(sockFd, FileBuffer(fileName));
    }, Async::Throw);

#undef OUT
}

void
Handler::onInput(const char* buffer, size_t len, const std::shared_ptr<Tcp::Peer>& peer) {
    auto& parser = getParser(peer);
    try {
        if (!parser.feed(buffer, len)) {
            parser.reset();
            throw HttpError(Code::Request_Entity_Too_Large, "Request exceeded maximum buffer size");
        }

        auto state = parser.parse();
        if (state == Private::State::Done) {
            ResponseWriter response(transport(), parser.request, this);
            response.associatePeer(peer);

#ifdef LIBSTDCPP_SMARTPTR_LOCK_FIXME
            parser.request.associatePeer(peer);
#endif
            onRequest(parser.request, std::move(response));
            parser.reset();
        }
    } catch (const HttpError &err) {
        ResponseWriter response(transport(), parser.request, this);
        response.associatePeer(peer);
        response.send(static_cast<Code>(err.code()), err.reason());
        parser.reset();
    }
    catch (const std::exception& e) {
        ResponseWriter response(transport(), parser.request, this);
        response.associatePeer(peer);
        response.send(Code::Internal_Server_Error, e.what());
        parser.reset();
    }
}

void
Handler::onConnection(const std::shared_ptr<Tcp::Peer>& peer) {
    peer->putData(ParserData, std::make_shared<Private::Parser<Http::Request>>());
}

void
Handler::onDisconnection(const shared_ptr<Tcp::Peer>& peer) {
    UNUSED(peer)
}

void
Handler::onTimeout(const Request& request, ResponseWriter response) {
    UNUSED(request)
    UNUSED(response)
}

void
Timeout::onTimeout(uint64_t numWakeup) {
    UNUSED(numWakeup)
    if (!peer.lock()) return;

    ResponseWriter response(transport, request, handler);
    response.associatePeer(peer);

    handler->onTimeout(request, std::move(response));
}


Private::Parser<Http::Request>&
Handler::getParser(const std::shared_ptr<Tcp::Peer>& peer) const {
    return *peer->getData<Private::Parser<Http::Request>>(ParserData);
}


} // namespace Http
} // namespace Pistache
