/* http.cc
   Mathieu Stefani, 13 August 2015
   
   Http layer implementation
*/

#include <cstring>
#include <iostream>
#include <stdexcept>
#include "http.h"
#include "net.h"

using namespace std;

namespace Net {

namespace Http {

namespace Private {

    static constexpr char CR = 0xD;
    static constexpr char LF = 0xA;

    void
    Parser::advance(size_t count) {
        if (cursor + count >= len) {
            throw std::runtime_error("Early EOF");
        }

        cursor += count;
    }

    bool
    Parser::eol() const {
        return buffer[cursor] == CR && buffer[cursor + 1] == LF;
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
            size_t start = cursor;

            while (buffer[cursor] != ':')
                advance(1);

            advance(1);

            std::string fieldName = std::string(buffer + start, cursor - start - 1);

            // Skip the ':'
            advance(1);

            start = cursor;
            while (!eol())
                advance(1);

            std::string fieldValue = std::string(buffer + start, cursor - start);

            advance(2);
            request.headers.push_back(make_pair(std::move(fieldName), std::move(fieldValue)));
        }
    }


} // namespace Private

Request::Request()
{ }


const char* methodString(Method method)
{
    static constexpr size_t MethodsCount
        = sizeof priv__methodStrings / sizeof *priv__methodStrings;

    int index = static_cast<int>(method);
    if (index >= MethodsCount)
        throw std::logic_error("Invalid method index");

    return priv__methodStrings[index];
}

void
Handler::onInput(const char* buffer, size_t len) {
    cout << "Received http request " << string(buffer, len) << endl;
    Private::Parser parser(buffer, len);
    parser.expectRequestLine();
    parser.expectHeaders();

    auto req = parser.request;

    cout << "method = " << methodString(req.method) << endl;
    cout << "resource = " << req.resource << endl;
    cout << "headers = " << endl;
    for (const auto& header: req.headers) {
        cout << string(' ', 4) << header.first << " -> " << header.second << endl;
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
Server::serve()
{
    listener.init(4);
    listener.setHandler(make_shared<Handler>());

    if (listener.bind()) { 
        const auto& addr = listener.address();
        cout << "Now listening on " << "http://" + addr.host() << ":" << addr.port() << endl;
        listener.run();
    }
}



} // namespace Http

} // namespace Net
