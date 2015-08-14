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
            throw std::runtime_error("Early EOF");
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
            throw std::runtime_error("Early EOF");
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
                cursor += len;
                return true;
            }
            return false;
        };

        // 5.1.1 Method

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

        advance(1);

        if (buffer[cursor] != ' ') {
            // Exceptionnado
        }

        // 5.1.2 Request-URI

        size_t start = cursor;

        while (buffer[cursor] != ' ') {
            advance(1);
            if (eol()) {
                // Exceptionnado
            }
        }

        request.resource = std::string(buffer + start, cursor - start); 

        // Skip HTTP-Version for now
        while (!eol())
            advance(1);

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

            std::string fieldName = std::string(buffer + start, cursor - start - 1);

            // Skip the ':'
            advance(1);

            // Read the header value
            start = cursor;
            while (!eol())
                advance(1);

            std::string fieldValue = std::string(buffer + start, cursor - start);

            if (fieldName == "Content-Length") {
                size_t pos;
                contentLength = std::stol(fieldValue, &pos);
            }

            request.headers.push_back(make_pair(std::move(fieldName), std::move(fieldValue)));


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

    return nullptr;
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

    return nullptr;
}

Message::Message()
{ }

Request::Request()
    : Message()
{ }

Response::Response(Code code, std::string body)
    : Message()
{
    this->body = std::move(body);
    code_ = code;
}

void
Response::writeTo(Tcp::Peer& peer)
{
    int fd = peer.fd();

    char buffer[Const::MaxBuffer];
    std::memset(buffer, 0, Const::MaxBuffer);
    char *p_buf = buffer;

    auto writeRaw = [&](const void* data, size_t len) {
        p_buf = static_cast<char *>(memcpy(p_buf, data, len));
        p_buf += len;
    };

    auto writeString = [&](const char* str) {
        const size_t len = std::strlen(str);
        writeRaw(str, std::strlen(str));
    };

    auto writeInt = [&](uint64_t value) {
        auto str = std::to_string(value);
        writeRaw(str.c_str(), str.size());
    };

    auto writeChar = [&](char c) {
        *p_buf++ = c;
    };

    writeString("HTTP/1.1 ");
    writeInt(static_cast<int>(code_));
    writeChar(' ');
    writeString(codeString(code_));
    writeRaw(CRLF, 2);

    writeString("Content-Length:");
    writeInt(body.size());
    writeRaw(CRLF, 2);

    writeRaw(CRLF, 2);
    writeString(body.c_str());

    const size_t len = p_buf - buffer;

    ssize_t bytes = send(fd, buffer, len, 0);
    cout << bytes << " bytes sent" << endl;
}

void
Handler::onInput(const char* buffer, size_t len, Tcp::Peer& peer) {
    Private::Parser parser(buffer, len);
    auto request = parser.expectRequest();

    onRequest(request, peer);
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

    listener.init(4);
    listener.setHandler(handler_);

    if (listener.bind()) { 
        const auto& addr = listener.address();
        cout << "Now listening on " << "http://" + addr.host() << ":" << addr.port() << endl;
        listener.run();
    }
}



} // namespace Http

} // namespace Net
