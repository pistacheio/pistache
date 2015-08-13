/* http.h
   Mathieu Stefani, 13 August 2015
   
   Http Layer
*/

#pragma once

#include "listener.h"
#include "net.h"

namespace Net {

namespace Http {

#define HTTP_METHODS \
    METHOD(Options, "OPTIONS") \
    METHOD(Get, "GET") \
    METHOD(Post, "POST") \
    METHOD(Head, "HEAD") \
    METHOD(Put, "PUT") \
    METHOD(Delete, "DELETE") \
    METHOD(Trace, "TRACE") \
    METHOD(Connect, "CONNECT")


enum class Method {
#define METHOD(m, _) m, 
    HTTP_METHODS
#undef METHOD
};

static constexpr const char* priv__methodStrings[] = {
#define METHOD(_, str) #str,
    HTTP_METHODS
#undef METHOD
};

class Request {
public:
    Request();

    Method method;
    std::string resource;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;

};


namespace Private {

    struct Parser {
        Parser(const char* buffer, size_t len)
            : buffer(buffer)
            , len(len)
            , cursor(0)
        { }

        void expectRequestLine();
        void expectHeaders();

        void advance(size_t count);
        bool eol() const;

        const char* buffer;
        size_t len;
        size_t cursor;


        Request request;
    };

}

class Handler : public Net::Tcp::Handler {
public:
    void onInput(const char* buffer, size_t len);
    void onOutput();
};

class Server {
public:
    Server();
    Server(const Net::Address& addr);

    void serve();

private:
    Net::Tcp::Listener listener;
};

} // namespace Http

} // namespace Net


