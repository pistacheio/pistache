/* http.cc
   Mathieu Stefani, 13 August 2015
   
   Http layer implementation
*/

#include <cstring>
#include <iostream>
#include <stdexcept>
#include "common.h"
#include "http.h"
#include "net.h"
#include "peer.h"
#include "array_buf.h"

using namespace std;

namespace Net {

namespace Http {

static constexpr char CR = 0xD;
static constexpr char LF = 0xA;
static constexpr char CRLF[] = {CR, LF};

template< class CharT, class Traits>
std::basic_ostream<CharT, Traits>& crlf(std::basic_ostream<CharT, Traits>& os) {
    os.write(CRLF, 2);
}

static constexpr const char* ParserData = "__Parser";

namespace Private {

    Parser::Buffer::Buffer()
        : len(0)
    {
        memset(data, sizeof data, 0);
    }

    void
    Parser::Buffer::reset() {
        memset(data, sizeof data, 0);
        len = 0;
    }

    bool
    Parser::Cursor::advance(size_t count)
    {
        if (value + count >= sizeof (buff.data)) {
            //parser->raise("Early EOF");
        }
        // Allowed to advance one past the end
        else if (value + count > buff.len) {
            return false;
        }

        value += count;
        return true;
    }

    bool
    Parser::Cursor::eol() const {
        return buff.data[value] == CR && next() == LF;
    }

    int
    Parser::Cursor::next() const {
        if (value + 1 >= sizeof (buff.data)) {
            //parser->raise("Early EOF");
        }

        else if (value + 1 >= buff.len) {
            return Eof;
        }

        return buff.data[value + 1];
    }

    char
    Parser::Cursor::current() const {
        return buff.data[value];
    }

    const char *
    Parser::Cursor::offset() const {
        return buff.data + value;
    }

    const char *
    Parser::Cursor::offset(size_t off) const {
        return buff.data + off;
    }

    size_t
    Parser::Cursor::diff(size_t other) const {
        return value - other;
    }

    size_t
    Parser::Cursor::diff(const Cursor& other) const {
        return value - other.value;
    }

    size_t
    Parser::Cursor::remaining() const {
        // assert(val <= buff.len);
        return buff.len - value;
    }

    void
    Parser::Cursor::reset() {
        value = 0;
    }

    void
    Parser::Step::raise(const char* msg, Code code /* = Code::Bad_Request */) {
        throw HttpError(code, msg);
    }

    Parser::State
    Parser::RequestLineStep::apply(Cursor& cursor) {
        Cursor::Revert revert(cursor);

        auto tryMatch = [&](const char* const str) {
            const size_t len = std::strlen(str);
            if (strncmp(cursor.offset(), str, len) == 0) {
                cursor.advance(len - 1);
                return true;
            }
            return false;
        };

        // Method

        if (tryMatch("OPTIONS")) {
           request->method = Method::Options;
        }
        else if (tryMatch("GET")) {
            request->method = Method::Get;
        }
        else if (tryMatch("POST")) {
            request->method = Method::Post;
        }
        else if (tryMatch("HEAD")) {
            request->method = Method::Head;
        }
        else if (tryMatch("PUT")) {
            request->method = Method::Put;
        }
        else if (tryMatch("DELETE")) {
            request->method = Method::Delete;
        }
        else {
            raise("Unknown HTTP request method");
        }

        auto n = cursor.next();
        if (n == Cursor::Eof) return State::Again;
        else if (n != ' ') raise("Malformed HTTP request after Method, expected SP");

        if (!cursor.advance(2)) return State::Again;

        size_t start = cursor;

        while ((n = cursor.next()) != Cursor::Eof && n != ' ') {
            if (!cursor.advance(1)) return State::Again;
        }

        request->resource = std::string(cursor.offset(start), cursor.diff(start) + 1);
        if ((n = cursor.next()) == Cursor::Eof) return State::Again;

        if (n != ' ')
            raise("Malformed HTTP request after Request-URI");

        // SP
        if (!cursor.advance(2)) return State::Again;

        // HTTP-Version
        start = cursor;

        while (!cursor.eol())
            if (!cursor.advance(1)) return State::Again;

        const size_t diff = cursor.diff(start);
        if (strncmp(cursor.offset(start), "HTTP/1.0", diff) == 0) {
            request->version = Version::Http10;
        }
        else if (strncmp(cursor.offset(start), "HTTP/1.1", diff) == 0) {
            request->version = Version::Http11;
        }
        else {
            raise("Encountered invalid HTTP version");
        }

        if (!cursor.advance(2)) return State::Again;

        revert.ignore();
        return State::Next;

    }

    Parser::State
    Parser::HeadersStep::apply(Cursor& cursor) {
        Cursor::Revert revert(cursor);

        while (!cursor.eol()) {
            Cursor::Revert headerRevert(cursor);

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

            if (HeaderRegistry::isRegistered(name)) {
                std::shared_ptr<Header> header = HeaderRegistry::makeHeader(name);
                header->parseRaw(cursor.offset(start), cursor.diff(start));
                request->headers.add(header);
            }

            // CRLF
            if (!cursor.advance(2)) return State::Again;

            headerRevert.ignore();
        }

        revert.ignore();
        return Parser::State::Next;
    }

    Parser::State
    Parser::BodyStep::apply(Cursor& cursor) {
        auto cl = request->headers.tryGet<ContentLength>();
        if (!cl) return Parser::State::Done;

        auto contentLength = cl->value();
        // We already started to read some bytes but we got an incomplete payload
        if (bytesRead > 0) {
            // How many bytes do we still need to read ?
            const size_t remaining = contentLength - bytesRead;
            auto start = cursor;

            // Could be refactored in a single function / lambda but I'm too lazy
            // for that right now
            if (!cursor.advance(remaining)) {
                const size_t available = cursor.remaining();

                request->body.append(cursor.offset(start), available);
                bytesRead += available;

                cursor.advance(available);
                return State::Again;
            }
            else {
                request->body.append(cursor.offset(), cursor.diff(start));
            }

        }
        // This is the first time we are reading the payload
        else {
            if (!cursor.advance(2)) return State::Again;

            request->body.reserve(contentLength);

            auto start = cursor;

            // We have an incomplete body, read what we can
            if (!cursor.advance(contentLength)) {
                const size_t available = cursor.remaining();

                request->body.append(cursor.offset(start), available);
                bytesRead += available;

                cursor.advance(available);
                return State::Again;
            }

            request->body.append(cursor.offset(start), cursor.diff(start));
        }

        bytesRead = 0;
        return Parser::State::Done;
    }

    Parser::State
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
        if (len + buffer.len >= sizeof (buffer.data)) {
            return false;
        }

        memcpy(buffer.data + buffer.len, data, len);
        buffer.len += len;

        return true;
    }

    void
    Parser::reset() {
        buffer.reset();
        cursor.reset();

        currentStep = 0;

        request.headers.clear();
        request.body.clear();
        request.resource.clear();
    }

} // namespace Private

const char* methodString(Method method)
{
    switch (method) {
#define METHOD(name, str) \
    case Method::name: \
        return str;
    HTTP_METHODS
#undef METHOD
    }

    unreachable();
}

const char* codeString(Code code)
{
    switch (code) {
#define CODE(_, name, str) \
    case Code::name: \
         return str;
    STATUS_CODES
#undef CODE
    }

    return "";
}

HttpError::HttpError(Code code, std::string reason)
    : code_(static_cast<int>(code))
    , reason_(std::move(reason))
{ }

HttpError::HttpError(int code, std::string reason)
    : code_(code)
    , reason_(std::move(reason))
{ }

Message::Message()
    : version(Version::Http11)
{ }

Request::Request()
    : Message()
{ }

Response::Response(int code, std::string body)
{
    this->body = std::move(body);
    code_ = code;
}

Response::Response(Code code, std::string body)
    : Message()
{
    this->body = std::move(body);
    code_ = static_cast<int>(code);
}

void
Response::writeTo(Tcp::Peer& peer)
{
    int fd = peer.fd();

    char buffer[Const::MaxBuffer];
    Io::OutArrayBuf obuf(buffer, Io::Init::ZeroOut);

    std::ostream stream(&obuf);
    stream << "HTTP/1.1 ";
    stream << code_;
    stream << ' ';
    stream << codeString(static_cast<Code>(code_));
    stream << crlf;

    for (const auto& header: headers.list()) {
        header->write(stream);
        stream << crlf;
    }

    stream << "Content-Length: " << body.size() << crlf;
    stream << crlf;

    stream << body;

    ssize_t bytes = send(fd, buffer, obuf.len(), 0);
}

void
Handler::onInput(const char* buffer, size_t len, Tcp::Peer& peer) {
    try {
        auto& parser = getParser(peer);
        if (!parser.feed(buffer, len)) {
            throw HttpError(Code::Request_Entity_Too_Large, "Request exceeded maximum buffer size");
        }

        auto state = parser.parse();
        if (state == Private::Parser::State::Done) {
            onRequest(parser.request, peer);
            parser.reset();
        }
    } catch (const HttpError &err) {
        Response response(err.code(), err.reason());
        response.writeTo(peer);
    }
    catch (const std::exception& e) {
        Response response(Code::Internal_Server_Error, e.what());
        response.writeTo(peer);
    }
}

void
Handler::onOutput() {
}

void
Handler::onConnection(Tcp::Peer& peer) {
    peer.putData(ParserData, std::make_shared<Private::Parser>());
}

void
Handler::onDisconnection(Tcp::Peer& peer) {
}

Private::Parser&
Handler::getParser(Tcp::Peer& peer) const {
    return *peer.getData<Private::Parser>(ParserData);
}

Endpoint::Endpoint()
{ }

Endpoint::Endpoint(const Net::Address& addr)
    : listener(addr)
{ }

void
Endpoint::setHandler(const std::shared_ptr<Handler>& handler) {
    handler_ = handler;
}

void
Endpoint::serve()
{
    if (!handler_)
        throw std::runtime_error("Must call setHandler() prior to serve()");

    listener.init(8, Tcp::Options::InstallSignalHandler);
    listener.setHandler(handler_);

    if (listener.bind()) { 
        const auto& addr = listener.address();
        cout << "Now listening on " << "http://" + addr.host() << ":" << addr.port() << endl;
        listener.run();
    }
}



} // namespace Http

} // namespace Net
