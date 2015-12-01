/* http.h
   Mathieu Stefani, 13 August 2015
   
   Http Layer
*/

#pragma once

#include <type_traits>
#include <stdexcept>
#include <array>
#include <sstream>
#include "listener.h"
#include "net.h"
#include "http_headers.h"
#include "http_defs.h"
#include "stream.h"
#include "mime.h"
#include "async.h"

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

    Message(const Message& other) = default;
    Message& operator=(const Message& other) = default;

    Message(Message&& other) = default;
    Message& operator=(Message&& other) = default;

    Version version_;
    Code code_;

    Header::Collection headers_;
    std::string body_;
};

namespace Uri {
    typedef std::string Fragment;

    class Query {
    public:
        Query();
        Query(std::initializer_list<std::pair<const std::string, std::string>> params);

        void add(std::string name, std::string value);
        Optional<std::string> get(const std::string& name) const;

    private:
        std::unordered_map<std::string, std::string> params;
    };
} // namespace Uri

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
    const Uri::Query& query() const;

private:
    Method method_;
    std::string resource_;
    Uri::Query query_;
};

class Handler;

class ResponseWriter : private Message {
public:
    ResponseWriter(const ResponseWriter& other) = delete;
    ResponseWriter& operator=(const ResponseWriter& other) = delete;

    ResponseWriter(ResponseWriter&& other)
        : Message(std::move(other))
        , peer_(std::move(other.peer_))
        , stream_(std::move(other.stream_))
    { }

    ResponseWriter& operator=(ResponseWriter&& other) {
        Message::operator=(std::move(other));
        peer_ = std::move(other.peer_);
        stream_ = std::move(other.stream_);

        return *this;
    }

    friend class Response;

    const Header::Collection& headers() const {
        return headers_;
    }

    Code code() const {
        return code_;
    }

    void setCode(Code code) {
        code_ = code;
    }

    std::streambuf* rdbuf() {
        return &stream_;
    }

    operator std::streambuf*() {
        return &stream_;
    }

    Async::Promise<ssize_t> send() const;

private:
    ResponseWriter(Message&& other, size_t size, std::weak_ptr<Tcp::Peer> peer)
        : Message(std::move(other))
        , stream_(size)
        , peer_(peer)
    {
    }

    ResponseWriter(const Message& other, size_t size, std::weak_ptr<Tcp::Peer> peer)
        : Message(other)
        , stream_(size)
        , peer_(peer)
    {
    }

    std::shared_ptr<Tcp::Peer> peer() const {
        if (peer_.expired()) {
            throw std::runtime_error("Broken pipe");
        }

        return peer_.lock();
    }

    NetworkStream stream_;
    std::weak_ptr<Tcp::Peer> peer_;
};

// 6. Response
class Response : private Message {
public:
    friend class Handler;

    static constexpr size_t DefaultStreamSize = 512;

    // C++11: std::weak_ptr move constructor is C++14 only so the default
    // version of move constructor / assignement operator does not work and we
    // have to define it ourself
    Response(Response&& other)
        : Message(std::move(other))
        , peer_(other.peer_)
    { }
    Response& operator=(Response&& other) {
        peer_ = other.peer_;
        return *this;
    }

    Response(const Response& other) = default;

    const Header::Collection& headers() const {
        return headers_;
    }

    Header::Collection& headers() {
        return headers_;
    }

    Code code() const {
        return code_;
    }

    void setMime(const Mime::MediaType& mime) {
        auto ct = headers_.tryGet<Header::ContentType>();
        if (ct)
            ct->setMime(mime);
        else
            headers_.add(std::make_shared<Header::ContentType>(mime));
    }

    Async::Promise<ssize_t> send(Code code) {
        return send(code, "");
    }
    Async::Promise<ssize_t> send(
            Code code,
            const std::string& body,
            const Mime::MediaType &mime = Mime::MediaType())
    {
        code_ = code;

        if (mime.isValid()) {
            auto contentType = headers_.tryGet<Header::ContentType>();
            if (contentType)
                contentType->setMime(mime);
            else
                headers_.add(std::make_shared<Header::ContentType>(mime));
        }

        ResponseWriter w(*this, body.size(), peer_);
        std::ostream os(w);
        os << body;
        if (!os)
            return Async::Promise<ssize_t>::rejected(Error("Response exceeded buffer size"));
        return w.send();
    }

    /* @Revisit: not sure about the name yet */
    ResponseWriter
    beginWrite(Code code = Code::Ok, size_t size = DefaultStreamSize) {
        code_ = code;
        return ResponseWriter(std::move(*this), size, peer_);
    }

    Async::Promise<Response *> timeoutAfter(std::chrono::milliseconds timeout);

private:
    Response()
        : Message()
    { }

    std::shared_ptr<Tcp::Peer> peer() const {
        if (peer_.expired()) {
            throw std::runtime_error("Broken pipe");
        }

        return peer_.lock();
    }

    void associatePeer(const std::shared_ptr<Tcp::Peer>& peer) {
        if (peer_.use_count() > 0)
            throw std::runtime_error("A peer was already associated to the response");

        peer_ = peer;
    }

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
            , cursor(&buffer)
        {
            allSteps[0].reset(new RequestLineStep(&request));
            allSteps[1].reset(new HeadersStep(&request));
            allSteps[2].reset(new BodyStep(&request));
        }

        Parser(const char* data, size_t len)
            : contentLength(-1)
            , currentStep(0)
            , cursor(&buffer)
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

    void onConnection(const std::shared_ptr<Tcp::Peer>& peer);
    void onDisconnection(const std::shared_ptr<Tcp::Peer>& peer);

    virtual void onRequest(const Request& request, Response response) = 0;

private:
    Private::Parser& getParser(const std::shared_ptr<Tcp::Peer>& peer) const;
};

class Endpoint {
public:

    struct Options {
        friend class Endpoint;

        Options& threads(int val);
        Options& flags(Flags<Tcp::Options> flags);
        Options& backlog(int val);

    private:
        int threads_;
        Flags<Tcp::Options> flags_;
        int backlog_;
        Options();
    };
    Endpoint();
    Endpoint(const Net::Address& addr);

    template<typename... Args>
    void initArgs(Args&& ...args) {
        listener.init(std::forward<Args>(args)...);
    }

    void init(const Options& options);

    void setHandler(const std::shared_ptr<Handler>& handler);
    void serve();

    static Options options();

private:
    std::shared_ptr<Handler> handler_;
    Net::Tcp::Listener listener;
};

} // namespace Http

} // namespace Net


