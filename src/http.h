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
#include "mime.h"

namespace Net {

namespace Http {

namespace Private {
    class Parser;
    class RequestLineStep;
    class HeadersStep;
    class BodyStep;
}

// 4. HTTP Message
class Message {
public:
    Message();
    Version version_;

    Header::Collection headers_;
    std::string body_;
};

// 5. Request
class Request : private Message {
public:
    friend class Private::RequestLineStep;
    friend class Private::HeadersStep;
    friend class Private::BodyStep;
    friend class Private::Parser;

    Request();

    Version version() const;
    Method method() const;
    std::string resource() const;

    std::string body() const;

    const Header::Collection& headers() const;

private:
    Method method_;
    std::string resource_;
};

class Handler;

// 6. Response
class Response : private Message {
public:
    friend class Handler;

    Response(const Response& other) = delete;
    Response& operator=(const Response& other) = delete;

    Response(Response&& other) = default;
    Response& operator=(Response&& other) = default;

    Header::Collection& headers();
    const Header::Collection& headers() const;

    void setMime(const Mime::MediaType& mime);

    ssize_t send(Code code);
    ssize_t send(Code code, const std::string& body, const Mime::MediaType &mime = Mime::MediaType());

private:
    Response();

    std::shared_ptr<Tcp::Peer> peer() const;

    void associatePeer(const std::shared_ptr<Tcp::Peer>& peer);
    std::weak_ptr<Tcp::Peer> peer_;
};

namespace Private {

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

    struct Parser {

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
    void onInput(const char* buffer, size_t len, const std::shared_ptr<Tcp::Peer>& peer);
    void onOutput();

    void onConnection(const std::shared_ptr<Tcp::Peer>& peer);
    void onDisconnection(const std::shared_ptr<Tcp::Peer>& peer);

    virtual void onRequest(const Request& request, Response response) = 0;

private:
    Private::Parser& getParser(const std::shared_ptr<Tcp::Peer>& peer) const;
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


