/* http.h
   Mathieu Stefani, 13 August 2015
   
   Http Layer
*/

#pragma once

#include <type_traits>
#include <stdexcept>
#include <array>
#include "listener.h"
#include "net.h"
#include "http_headers.h"

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

// 10. Status Code Definitions
#define STATUS_CODES \
    CODE(100, Continue, "Continue") \
    CODE(101, Switching_Protocols, "Switching Protocols") \
    CODE(200, Ok, "OK") \
    CODE(201, Created, "Created") \
    CODE(202, Accepted, "Accepted") \
    CODE(203, NonAuthoritative_Information, "Non-Authoritative Information") \
    CODE(204, No_Content, "No Content") \
    CODE(205, Reset_Content, "Reset Content") \
    CODE(206, Partial_Content, "Partial Content") \
    CODE(300, Multiple_Choices, "Multiple Choices") \
    CODE(301, Moved_Permanently, "Moved Permanently") \
    CODE(302, Found, "Found") \
    CODE(303, See_Other, "See Other") \
    CODE(304, Not_Modified, "Not Modified") \
    CODE(305, Use_Proxy, "Use Proxy") \
    CODE(307, Temporary_Redirect, "Temporary Redirect") \
    CODE(400, Bad_Request, "Bad Request") \
    CODE(401, Unauthorized, "Unauthorized") \
    CODE(402, Payment_Required, "Payment Required") \
    CODE(403, Forbidden, "Forbidden") \
    CODE(404, Not_Found, "Not Found") \
    CODE(405, Method_Not_Allowed, "Method Not Allowed") \
    CODE(406, Not_Acceptable, "Not Acceptable") \
    CODE(407, Proxy_Authentication_Required, "Proxy Authentication Required") \
    CODE(408, Request_Timeout, "Request Timeout") \
    CODE(409, Conflict, "Conflict") \
    CODE(410, Gone, "Gone") \
    CODE(411, Length_Required, "Length Required") \
    CODE(412, Precondition_Failed, "Precondition Failed") \
    CODE(413, Request_Entity_Too_Large, "Request Entity Too Large") \
    CODE(414, RequestURI_Too_Long, "Request-URI Too Long") \
    CODE(415, Unsupported_Media_Type, "Unsupported Media Type") \
    CODE(416, Requested_Range_Not_Satisfiable, "Requested Range Not Satisfiable") \
    CODE(417, Expectation_Failed, "Expectation Failed") \
    CODE(500, Internal_Server_Error, "Internal Server Error") \
    CODE(501, Not_Implemented, "Not Implemented") \
    CODE(502, Bad_Gateway, "Bad Gateway") \
    CODE(503, Service_Unavailable, "Service Unavailable") \
    CODE(504, Gateway_Timeout, "Gateway Timeout")


enum class Method {
#define METHOD(m, _) m, 
    HTTP_METHODS
#undef METHOD
};

enum class Code {
#define CODE(value, name, _) name = value,
    STATUS_CODES
#undef CODE
};

enum class Version {
    Http10, // HTTP/1.0
    Http11 // HTTP/1.1
};

const char* methodString(Method method);
const char* codeString(Code code);

struct HttpError : public std::exception {
    HttpError(Code code, std::string reason);
    HttpError(int code, std::string reason);

    ~HttpError() noexcept { }

    const char* what() const noexcept { return reason_.c_str(); }

    int code() const { return code_; }
    std::string reason() const { return reason_; }

private:
    int code_;
    std::string reason_;
};

// 4. HTTP Message
class Message {
public:
    Message();
    Version version;

    Headers headers;
    std::string body;
};

// 5. Request
class Request : public Message {
public:
    Request();

    Method method;
    std::string resource;
};

// 6. Response
class Response : public Message {
public:
    Response(int code, std::string body);
    Response(Code code, std::string body);

    void writeTo(Tcp::Peer& peer);
    std::string mimeType;

private:
    int code_;
};

namespace Private {

    struct Parser {

        struct Buffer {
            Buffer();

            char data[Const::MaxBuffer];
            size_t len;

            void reset();
        };

        struct Cursor {

            struct Revert {
                Revert(Cursor& cursor)
                    : cursor(cursor)
                    , pos(cursor.value)
                    , active(true)
                { }

                void revert() {
                    cursor.value = pos;
                }

                void ignore() {
                    active = false;
                }

                ~Revert() {
                    if (active) cursor.value = pos;
                }

                Cursor& cursor;

                size_t pos;
                bool active;
            };

            static constexpr int Eof = -1;

            Cursor(const Buffer &buffer, size_t initialPos = 0)
                : buff(buffer)
                , value(initialPos)
            { }

            bool advance(size_t count);

            operator size_t() const { return value; }

            bool eol() const;
            int next() const;
            char current() const;

            const char *offset() const;
            const char *offset(size_t off) const;

            size_t diff(size_t other) const;
            size_t diff(const Cursor& other) const;
            size_t remaining() const;

            void reset();

        private:
            const Buffer& buff;
            size_t value;
        };

        enum class State { Again, Next, Done };

        struct Step {
            Step(Request* request)
                : request(request)
            { }

            virtual State apply(Cursor& cursor) = 0;

            void raise(const char* msg, Code code = Code::Bad_Request);

            Request *request;
        };

        struct RequestLineStep : public Step {
            RequestLineStep(Request* request)
                : Step(request)
            { }

            State apply(Cursor& cursor);
        };

        struct HeadersStep : public Step {
            HeadersStep(Request* request)
                : Step(request)
            { }

            State apply(Cursor& cursor);
        };

        struct BodyStep : public Step {
            BodyStep(Request* request)
                : Step(request)
                , bytesRead(0)
            { }

            State apply(Cursor& cursor);

        private:
            size_t bytesRead;
        };

        Parser()
            : contentLength(-1)
            , currentStep(0)
            , cursor(buffer)
        {
            allSteps[0].reset(new RequestLineStep(&request));
            allSteps[1].reset(new HeadersStep(&request));
            allSteps[2].reset(new BodyStep(&request));
        }

        Parser(const char* data, size_t len)
            : contentLength(-1)
            , currentStep(0)
            , cursor(buffer)
        {
            allSteps[0].reset(new RequestLineStep(&request));
            allSteps[1].reset(new HeadersStep(&request));
            allSteps[2].reset(new BodyStep(&request));

            feed(data, len);
        }

        Parser(const Parser& other) = delete;
        Parser(Parser&& other) = default;

        bool feed(const char* data, size_t len);
        void reset();

        State parse();

        Buffer buffer;
        Cursor cursor;

        Request request;

    private:
        static constexpr size_t StepsCount = 3;

        std::array<std::unique_ptr<Step>, StepsCount> allSteps;
        size_t currentStep;

        ssize_t contentLength;
    };

    struct Writer {
        Writer(char* buffer, size_t len) 
            : buf(buffer)
            , len(len)
        { }

        ssize_t writeRaw(const void* data, size_t len);
        ssize_t writeString(const char* str);

        template<typename T>
        typename std::enable_if<
                     std::is_integral<T>::value, ssize_t
                  >::type
        writeInt(T value) {
            auto str = std::to_string(value);
            return writeRaw(str.c_str(), str.size());
        }

        ssize_t writeChar(char c) {
            *buf++ = c;
            return 0;
        }

        ssize_t writeHeader(const char* name, const char* value);

        template<typename T>
        typename std::enable_if<
                    std::is_arithmetic<T>::value, ssize_t
                 >::type
        writeHeader(const char* name, T value) {
            auto str = std::to_string(value);
            return writeHeader(name, str.c_str());
        }

        char *cursor() const { return buf; }

    private:
        char* buf;
        size_t len;
    };

}

class Handler : public Net::Tcp::Handler {
public:
    void onInput(const char* buffer, size_t len, Tcp::Peer& peer);
    void onOutput();

    void onConnection(Tcp::Peer& peer);
    void onDisconnection(Tcp::Peer& peer);

    virtual void onRequest(const Request& request, Tcp::Peer& peer) = 0;

private:
    Private::Parser& getParser(Tcp::Peer& peer) const;
};

class Endpoint {
public:
    Endpoint();
    Endpoint(const Net::Address& addr);

    void setHandler(const std::shared_ptr<Handler>& handler);
    void serve();

private:
    std::shared_ptr<Handler> handler_;
    Net::Tcp::Listener listener;
};

} // namespace Http

} // namespace Net


