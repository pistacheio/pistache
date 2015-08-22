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

using namespace std;

namespace Net {

namespace Http {

static constexpr char CR = 0xD;
static constexpr char LF = 0xA;
static constexpr char CRLF[] = {CR, LF};

namespace Private {

    Parser::Buffer::Buffer()
        : len(0)
    {
        memset(data, sizeof data, 0);
    }

    bool
    Parser::Cursor::advance(size_t count)
    {
        if (value + count >= sizeof (buff.data)) {
            //parser->raise("Early EOF");
        }
        else if (value + count >= buff.len) {
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
    Parser::Cursor::diff(size_t previous) const {
        return value - previous;
    }

    void
    Parser::Step::raise(const char* msg) {
        throw ParsingError(msg);
    }

    Parser::State
    Parser::RequestLineStep::apply(Cursor& cursor) {
        Reverter reverter(cursor);

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
            raise("Lol wat");
        }

        auto n = cursor.next();
        if (n == Cursor::Eof) return State::Again;
        else if (n != ' ') raise("Malformed HTTP Request");


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

        reverter.clear();
        return State::Next;

    }

    Parser::State
    Parser::HeadersStep::apply(Cursor& cursor) {
        Reverter reverter(cursor);

        while (!cursor.eol()) {
            Reverter headerReverter(cursor);

            // Read the header name
            size_t start = cursor;

            while (cursor.current() != ':')
                if (!cursor.advance(1)) return State::Again;

            if (!cursor.advance(1)) return State::Again;

            std::string name = std::string(cursor.offset(start), cursor.diff(start) - 1);

            // Skip the ':'
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

            headerReverter.clear();
        }

        return Parser::State::Next;
    }

    Parser::State
    Parser::BodyStep::apply(Cursor& cursor) {
        auto cl = request->headers.tryGet<ContentLength>();

        if (cl) {
            // CRLF
            if (!cursor.advance(2)) return State::Again;

            auto len = cl->value();
            auto start = cursor;

            if (!cursor.advance(len)) return State::Again;

            request->body = std::string(cursor.offset(start), cursor.diff(start));
        }

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
    }

    ssize_t
    Writer::writeRaw(const void* data, size_t len) {
        buf = static_cast<char *>(memcpy(buf, data, len));
        buf += len;

        return 0;
    }

    ssize_t
    Writer::writeString(const char* str) {
        const size_t len = std::strlen(str);
        return writeRaw(str, std::strlen(str));
    }

    ssize_t
    Writer::writeHeader(const char* name, const char* value) {
        writeString(name);
        writeChar(':');
        writeString(value);
        writeRaw(CRLF, 2);

        return 0;
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

    unreachable();
}

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
    std::memset(buffer, 0, Const::MaxBuffer);
    Private::Writer fmt(buffer, sizeof buffer);

    fmt.writeString("HTTP/1.1 ");
    fmt.writeInt(code_);
    fmt.writeChar(' ');
    fmt.writeString(codeString(static_cast<Code>(code_)));
    fmt.writeRaw(CRLF, 2);

    for (const auto& header: headers.list()) {
        std::ostringstream oss;
        header->write(oss);

        std::string str = oss.str();
        fmt.writeRaw(str.c_str(), str.size());
        fmt.writeRaw(CRLF, 2);
    }

    fmt.writeHeader("Content-Length", body.size());

    fmt.writeRaw(CRLF, 2);
    fmt.writeString(body.c_str());

    const size_t len = fmt.cursor() - buffer;

    ssize_t bytes = send(fd, buffer, len, 0);
}

void
Handler::onInput(const char* buffer, size_t len, Tcp::Peer& peer) {
    Private::Parser parser(buffer, len);
    try {
        Private::Parser::State state = parser.parse();
        if (state == Private::Parser::State::Done) {
            onRequest(parser.request, peer);
        }
    } catch (const Private::ParsingError &err) {
        cerr << "Error when parsing HTTP request: " << err.what() << endl;
    }
}

void
Handler::onOutput() {
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
