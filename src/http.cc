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

    void
    Parser::advance(size_t count) {
        if (cursor + count >= len) {
            raise("Early EOF");
        }

        cursor += count;
    }

    bool
    Parser::eol() const {
        return buffer[cursor] == CR && next() == LF;
    }

    char
    Parser::next() const {
        if (cursor + 1 >= len) {
            raise("Early EOF");
        }

        return buffer[cursor + 1];
    }

    Request
    Parser::expectRequest() {
        expectRequestLine();
        expectHeaders();
        expectBody();

        return request;
    }

    // 5.1 Request-Line
    void
    Parser::expectRequestLine() {

        auto tryMatch = [&](const char* const str) {
            const size_t len = std::strlen(str);
            if (strncmp(buffer, str, len) == 0) {
                cursor += len - 1;
                return true;
            }
            return false;
        };

        // Method

        if (tryMatch("OPTIONS")) {
           request.method = Method::Options;
        }
        else if (tryMatch("GET")) {
            request.method = Method::Get;
        }
        else if (tryMatch("POST")) {
            request.method = Method::Post;
        }
        else if (tryMatch("HEAD")) {
            request.method = Method::Head;
        }
        else if (tryMatch("PUT")) {
            request.method = Method::Put;
        }
        else if (tryMatch("DELETE")) {
            request.method = Method::Delete;
        }

        if (next() != ' ') {
            raise("Malformed HTTP request after Method");
        }

        // SP
        advance(2);

        // Request-URI

        size_t start = cursor;

        while (next() != ' ') {
            advance(1);
            if (eol()) {
                raise("Malformed HTTP request after Request-URI");
            }
        }

        request.resource = std::string(buffer + start, cursor - start + 1); 
        if (next() != ' ') {
            raise("Malformed HTTP request after Request-URI");
        }

        // SP
        advance(2);

        // HTTP-Version
        start = cursor;

        while (!eol())
            advance(1);

        const size_t diff = cursor - start;
        if (strncmp(buffer + start, "HTTP/1.0", diff) == 0) {
            request.version = Version::Http10;
        }
        else if (strncmp(buffer + start, "HTTP/1.1", diff) == 0) {
            request.version = Version::Http11;
        }
        else {
            raise("Encountered invalid HTTP version");
        }

        advance(2);

    }

    void
    Parser::expectHeaders() {
        while (!eol()) {
            // Read the header name
            size_t start = cursor;

            while (buffer[cursor] != ':')
                advance(1);

            advance(1);

            std::string name = std::string(buffer + start, cursor - start - 1);

            // Skip the ':'
            advance(1);

            // Read the header value
            start = cursor;
            while (!eol())
                advance(1);


            if (HeaderRegistry::isRegistered(name)) {
                std::shared_ptr<Header> header = HeaderRegistry::makeHeader(name);
                header->parseRaw(buffer + start, cursor - start);
                request.headers.add(header);
            }
            else {
                std::string value = std::string(buffer + start, cursor - start);
            }

            // CRLF
            advance(2);
        }
    }

    void
    Parser::expectBody() {
        if (contentLength > 0) {
            advance(2); // CRLF

            if (cursor + contentLength > len) {
                throw std::runtime_error("Corrupted HTTP Body");
            }

            request.body = std::string(buffer + cursor, contentLength);
        }
    }

    void
    Parser::raise(const char* msg) const {
        throw ParsingError(msg);
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

    fmt.writeHeader("Content-Length", body.size());

    fmt.writeRaw(CRLF, 2);
    fmt.writeString(body.c_str());

    const size_t len = fmt.cursor() - buffer;

    ssize_t bytes = send(fd, buffer, len, 0);
    //cout << bytes << " bytes sent" << endl;
}

void
Handler::onInput(const char* buffer, size_t len, Tcp::Peer& peer) {
    Private::Parser parser(buffer, len);
    try {
        auto request = parser.expectRequest();
        onRequest(request, peer);
    } catch (const Private::ParsingError &err) {
        cerr << "Error when parsing HTTP request: " << err.what() << endl;
    }
}

void
Handler::onOutput() {
}

Server::Server()
{ }

Server::Server(const Net::Address& addr)
    : listener(addr)
{ }

void
Server::setHandler(const std::shared_ptr<Handler>& handler) {
    handler_ = handler;
}

void
Server::serve()
{
    if (!handler_)
        throw std::runtime_error("Must call setHandler() prior to serve()");

    listener.init(8,
            Tcp::Options::NoDelay |
            Tcp::Options::InstallSignalHandler |
            Tcp::Options::ReuseAddr);
    listener.setHandler(handler_);

    if (listener.bind()) { 
        const auto& addr = listener.address();
        cout << "Now listening on " << "http://" + addr.host() << ":" << addr.port() << endl;
        listener.run();
    }
}



} // namespace Http

} // namespace Net
