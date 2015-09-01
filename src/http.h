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
#include "http_defs.h"

namespace Net {

namespace Http {

// 4. HTTP Message
class Message {
public:
    Message();
    Version version;

    Header::Collection headers;
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


