/* http.cc
   Mathieu Stefani, 13 August 2015
   
   Http layer implementation
*/

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <ctime>
#include <iomanip>
#include "common.h"
#include "http.h"
#include "net.h"
#include "peer.h"
#include "array_buf.h"

using namespace std;

namespace Net {

namespace Http {

template< class CharT, class Traits>
std::basic_ostream<CharT, Traits>& crlf(std::basic_ostream<CharT, Traits>& os) {
    static constexpr char CRLF[] = {0xD, 0xA};
    os.write(CRLF, 2);
}

template<typename H, typename Stream, typename... Args>
typename std::enable_if<Header::IsHeader<H>::value, void>::type
writeHeader(Stream& stream, Args&& ...args) {
    H header(std::forward<Args>(args)...);

    stream << H::Name << ": ";
    header.write(stream);

    stream << crlf;
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
                if (!match_until('=', cursor))
                    return State::Again;

                std::string key = keyToken.text();

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

            if (Header::Registry::isRegistered(name)) {
                std::shared_ptr<Header::Header> header = Header::Registry::makeHeader(name);
                header->parseRaw(cursor.offset(start), cursor.diff(start));
                request->headers_.add(header);
            }
            else {
                std::string value(cursor.offset(start), cursor.diff(start));
                request->headers_.addRaw(Header::Raw(std::move(name), std::move(value)));
            }

            // CRLF
            if (!cursor.advance(2)) return State::Again;

            headerRevert.ignore();
        }

        revert.ignore();
        return State::Next;
    }

    State
    BodyStep::apply(StreamCursor& cursor) {
        auto cl = request->headers_.tryGet<Header::ContentLength>();
        if (!cl) return State::Done;

        auto contentLength = cl->value();
        // We already started to read some bytes but we got an incomplete payload
        if (bytesRead > 0) {
            // How many bytes do we still need to read ?
            const size_t remaining = contentLength - bytesRead;

            StreamCursor::Token token(cursor);
            const size_t available = cursor.remaining();

            // Could be refactored in a single function / lambda but I'm too lazy
            // for that right now
            if (available < remaining) {
                cursor.advance(available);
                request->body_ += token.text();

                bytesRead += available;

                return State::Again;
            }
            else {
                cursor.advance(remaining);
                request->body_ += token.text();
            }

        }
        // This is the first time we are reading the payload
        else {
            if (!cursor.advance(2)) return State::Again;

            request->body_.reserve(contentLength);

            StreamCursor::Token token(cursor);
            const size_t available = cursor.remaining();
            // We have an incomplete body, read what we can
            if (available < contentLength) {
                cursor.advance(available);
                request->body_ += token.text();
                bytesRead += available;
                return State::Again;
            }

            cursor.advance(contentLength);
            request->body_ = token.text();
        }

        bytesRead = 0;
        return State::Done;
    }

    State
    Parser::parse() {
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
    Parser::feed(const char* data, size_t len) {
        return buffer.feed(data, len);
    }

    void
    Parser::reset() {
        buffer.reset();
        cursor.reset();

        currentStep = 0;

        request.headers_.clear();
        request.body_.clear();
        request.resource_.clear();
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

Async::Promise<Response *>
Response::timeoutAfter(std::chrono::milliseconds timeout)
{
#if 0
    Async::Promise<uint64_t> promise([=](Async::Resolver& resolve, Async::Rejection& reject) {
       peer()->io()->setTimeout(timeout, resolve, reject);
   }); 

    promise.then([=](uint64_t) {
        send(Code::Bad_Request, "A timeout occured");
    }, Async::NoExcept);

    return promise;
#endif
    Async::Promise<uint64_t> promise([=](Async::Resolver& resolve, Async::Rejection& reject) {
       peer()->io()->setTimeout(timeout, resolve, reject);
   }); 

    return promise.then([=](uint64_t) {
        auto p = Async::Promise<Response *>::resolved(this);

        return p;
    }, Async::NoExcept);
}

Async::Promise<ssize_t>
ResponseWriter::send() const
{
    auto body = stream_.buffer();

    NetworkStream stream(512 + body.len);
    std::ostream os(&stream);

#define OUT(...) \
    do { \
        __VA_ARGS__; \
        if (!os) { \
            return Async::Promise<ssize_t>::rejected(Error("Response exceeded buffer size")); \
        } \
    } while (0);

    OUT(os << "HTTP/1.1 ");
    OUT(os << static_cast<int>(code_));
    OUT(os << ' ');
    OUT(os << code_);
    OUT(os << crlf);

    for (const auto& header: headers_.list()) {
        OUT(os << header->name() << ": ");
        OUT(header->write(os));
        OUT(os << crlf);
    }

    OUT(writeHeader<Header::ContentLength>(os, body.len));
    OUT(os << crlf);

    OUT(os.write(static_cast<const char *>(body.data), body.len));

    auto buf = stream.buffer();

    return peer()->send(buf.data, buf.len);
}

void
Handler::onInput(const char* buffer, size_t len, const std::shared_ptr<Tcp::Peer>& peer) {
    try {
        auto& parser = getParser(peer);
        if (!parser.feed(buffer, len)) {
            parser.reset();
            throw HttpError(Code::Request_Entity_Too_Large, "Request exceeded maximum buffer size");
        }

        auto state = parser.parse();
        if (state == Private::State::Done) {
            Response response;
            response.associatePeer(peer);
            onRequest(parser.request, std::move(response));
            parser.reset();
        }
    } catch (const HttpError &err) {
        Response response;
        response.associatePeer(peer);
        response.send(static_cast<Code>(err.code()), err.reason());
        getParser(peer).reset();
    }
    catch (const std::exception& e) {
        Response response;
        response.associatePeer(peer);
        response.send(Code::Internal_Server_Error, e.what());
        getParser(peer).reset();
    }
}

void
Handler::onConnection(const std::shared_ptr<Tcp::Peer>& peer) {
    peer->putData(ParserData, std::make_shared<Private::Parser>());
}

void
Handler::onDisconnection(const shared_ptr<Tcp::Peer>& peer) {
}

Private::Parser&
Handler::getParser(const std::shared_ptr<Tcp::Peer>& peer) const {
    return *peer->getData<Private::Parser>(ParserData);
}

Endpoint::Options::Options()
    : threads_(1)
{ }

Endpoint::Options&
Endpoint::Options::threads(int val) {
    threads_ = val;
    return *this;
}

Endpoint::Options&
Endpoint::Options::flags(Flags<Tcp::Options> flags) {
    flags_ = flags;
    return *this;
}

Endpoint::Options&
Endpoint::Options::backlog(int val) {
    backlog_ = val;
    return *this;
}

Endpoint::Endpoint()
{ }

Endpoint::Endpoint(const Net::Address& addr)
    : listener(addr)
{ }

void
Endpoint::init(const Endpoint::Options& options) {
    listener.init(options.threads_, options.flags_);
}

void
Endpoint::setHandler(const std::shared_ptr<Handler>& handler) {
    handler_ = handler;
}

void
Endpoint::serve()
{
    if (!handler_)
        throw std::runtime_error("Must call setHandler() prior to serve()");

    listener.setHandler(handler_);

    if (listener.bind()) { 
        const auto& addr = listener.address();
        cout << "Now listening on " << "http://" + addr.host() << ":" << addr.port() << endl;
        listener.run();
    }
}

Endpoint::Options
Endpoint::options() {
    return Options();
}


} // namespace Http

} // namespace Net
