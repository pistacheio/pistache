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
#include "stream.h"

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

        enum class State { Again, Next, Done };

        struct Step {
            Step(Request* request)
                : request(request)
            { }

            virtual State apply(StreamCursor& cursor) = 0;

            void raise(const char* msg, Code code = Code::Bad_Request);

            Request *request;
        };

        struct RequestLineStep : public Step {
            RequestLineStep(Request* request)
                : Step(request)
            { }

            State apply(StreamCursor& cursor);
        };

        struct HeadersStep : public Step {
            HeadersStep(Request* request)
                : Step(request)
            { }

            State apply(StreamCursor& cursor);
        };

        struct BodyStep : public Step {
            BodyStep(Request* request)
                : Step(request)
                , bytesRead(0)
            { }

            State apply(StreamCursor& cursor);

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

       // Buffer buffer;
       // Cursor cursor;
        ArrayStreamBuf<Const::MaxBuffer> buffer;
        StreamCursor cursor;

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


