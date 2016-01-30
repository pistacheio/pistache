/* http.h
   Mathieu Stefani, 13 August 2015
   
   Http Layer
*/

#pragma once

#include <type_traits>
#include <stdexcept>
#include <array>
#include <sstream>
#include "net.h"
#include "http_headers.h"
#include "http_defs.h"
#include "cookie.h"
#include "stream.h"
#include "mime.h"
#include "async.h"
#include "peer.h"
#include "tcp.h"
#include "transport.h"

namespace Net {

namespace Http {

namespace Private {
    class ParserBase;
    template<typename T> struct Parser;
    class RequestLineStep;
    class HeadersStep;
    class BodyStep;
}

template< class CharT, class Traits>
std::basic_ostream<CharT, Traits>& crlf(std::basic_ostream<CharT, Traits>& os) {
    static constexpr char CRLF[] = {0xD, 0xA};
    os.write(CRLF, 2);

    return os;
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

    CookieJar cookies_;
};

namespace Uri {
    typedef std::string Fragment;

    class Query {
    public:
        Query();
        Query(std::initializer_list<std::pair<const std::string, std::string>> params);

        void add(std::string name, std::string value);
        Optional<std::string> get(const std::string& name) const;
        bool has(const std::string& name) const;

        void clear() {
            params.clear();
        }

    private:
        std::unordered_map<std::string, std::string> params;
    };
} // namespace Uri

class RequestBuilder;

// 5. Request
class Request : private Message {
public:
    friend class Private::RequestLineStep;
    friend class Private::HeadersStep;
    friend class Private::BodyStep;
    friend class Private::ParserBase;
    friend class Private::Parser<Http::Request>;

    friend class RequestBuilder;

    Request(const Request& other) = default;
    Request& operator=(const Request& other) = default;

    Request(Request&& other) = default;
    Request& operator=(Request&& other) = default;


    Version version() const;
    Method method() const;
    std::string resource() const;

    std::string body() const;

    const Header::Collection& headers() const;
    const Uri::Query& query() const;

    const CookieJar& cookies() const;

    /* @Investigate: this is disabled because of a lock in the shared_ptr / weak_ptr
        implementation of libstdc++. Under contention, we experience a performance
        drop of 5x with that lock

        If this turns out to be a problem, we might be able to replace the weak_ptr
        trick to detect peer disconnection by a plain old "observer" pointer to a 
        tcp connection with a "stale" state
    */
#ifdef LIBSTDCPP_SMARTPTR_LOCK_FIXME
    std::shared_ptr<Tcp::Peer> peer() const;
#endif

private:
    Request();

#ifdef LIBSTDCPP_SMARTPTR_LOCK_FIXME
    void associatePeer(const std::shared_ptr<Tcp::Peer>& peer) {
        if (peer_.use_count() > 0)
            throw std::runtime_error("A peer was already associated to the response");

        peer_ = peer;
    }
#endif

    Method method_;
    std::string resource_;
    Uri::Query query_;

#ifdef LIBSTDCPP_SMARTPTR_LOCK_FIXME
    std::weak_ptr<Tcp::Peer> peer_;
#endif
};

class RequestBuilder {
public:
    RequestBuilder& resource(std::string val);
    RequestBuilder& params(const Uri::Query& query);
    RequestBuilder& header(const std::shared_ptr<Header::Header>& header);

    template<typename H, typename... Args>
    typename
    std::enable_if<
        Header::IsHeader<H>::value, RequestBuilder&
    >::type
    header(Args&& ...args) {
        return header(std::make_shared<H>(std::forward<Args>(args)...));
    }

    RequestBuilder& cookie(const Cookie& cookie);
    RequestBuilder& body(std::string val);

    const Request& request() const { return request_; }
    operator Request() const { return request_; }
private:
    Request request_;
};

class Handler;
class Response;

class Timeout {
public:

    friend class Handler;

    template<typename Duration>
    void arm(Duration duration) {
        Async::Promise<uint64_t> p([=](Async::Resolver& resolve, Async::Rejection& reject) {
            transport->io()->armTimer(duration, std::move(resolve), std::move(reject));
        });

        p.then(
            [=](uint64_t numWakeup) {
                this->armed = false;
                this->onTimeout(numWakeup);
        },
        [=](std::exception_ptr exc) {
            std::rethrow_exception(exc);
        });

        armed = true;
    }

    void disarm() {
        transport->io()->disarmTimer();
        armed = false;
    }

    bool isArmed() const {
        return armed;
    }

private:
    Timeout(Tcp::Transport* transport,
            Handler* handler,
            const std::shared_ptr<Tcp::Peer>& peer,
            const Request& request)
        : transport(transport)
        , handler(handler)
        , peer(peer)
        , request(request)
        , armed(false)
    { }

    void onTimeout(uint64_t numWakeup);

    Handler* handler;
    std::weak_ptr<Tcp::Peer> peer;
    Request request;

    Tcp::Transport* transport;
    bool armed;
};

class ResponseStream : private Message {
public:
    friend class Response;

    ResponseStream(ResponseStream&& other)
        : Message(std::move(other))
        , peer_(std::move(other.peer_))
        , buf_(std::move(other.buf_))
        , transport_(other.transport_)
    { }

    ResponseStream& operator=(ResponseStream&& other) {
        Message::operator=(std::move(other));
        peer_ = std::move(other.peer_);
        buf_ = std::move(other.buf_);
        transport_ = other.transport_;

        return *this;
    }

    template<typename T>
    friend
    ResponseStream& operator<<(ResponseStream& stream, const T& val);

    const Header::Collection& headers() const {
        return headers_;
    }

    const CookieJar& cookies() const {
        return cookies_;
    }

    Code code() const {
        return code_;
    }

    void flush();
    void ends();

private:
    ResponseStream(
            Message&& other,
            std::weak_ptr<Tcp::Peer> peer,
            Tcp::Transport* transport,
            size_t streamSize);

    std::shared_ptr<Tcp::Peer> peer() const {
        if (peer_.expired())
            throw std::runtime_error("Write failed: Broken pipe");

        return peer_.lock();
    }

    std::weak_ptr<Tcp::Peer> peer_;
    DynamicStreamBuf buf_;
    Tcp::Transport* transport_;
};

inline ResponseStream& ends(ResponseStream &stream) {
    stream.ends();
    return stream;
}

inline ResponseStream& flush(ResponseStream& stream) {
    stream.flush();
    return stream;
}

template<typename T>
ResponseStream& operator<<(ResponseStream& stream, const T& val) {
    Net::Size<T> size;

    std::ostream os(&stream.buf_);
    os << size(val) << crlf;
    os << val << crlf;

    return stream;
}

inline ResponseStream&
operator<<(ResponseStream& stream, ResponseStream & (*func)(ResponseStream &)) {
    return (*func)(stream);
}

// 6. Response
class Response : public Message {
public:
    friend class Handler;
    friend class Timeout;
    friend class Private::ParserBase;
    friend class Private::Parser<Response>;

    friend Async::Promise<ssize_t> serveFile(Response&, const char *, const Mime::MediaType&);

    static constexpr size_t DefaultStreamSize = 512;

    // C++11: std::weak_ptr move constructor is C++14 only so the default
    // version of move constructor / assignement operator does not work and we
    // have to define it ourself
    Response(Response&& other)
        : Message(std::move(other))
        , peer_(other.peer_)
        , buf_(std::move(other.buf_))
        , transport_(other.transport_)
    { }
    Response& operator=(Response&& other) {
        Message::operator=(std::move(other));
        peer_ = std::move(other.peer_);
        transport_ = other.transport_;
        buf_ = std::move(other.buf_);
        return *this;
    }

    const Header::Collection& headers() const {
        return headers_;
    }

    Header::Collection& headers() {
        return headers_;
    }

    const CookieJar& cookies() const {
        return cookies_;
    }

    CookieJar& cookies() {
        return cookies_;
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
        code_ = code;
        return putOnWire(nullptr, 0);
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

        return putOnWire(body.c_str(), body.size());
    }

    template<size_t N>
    Async::Promise<ssize_t> send(
            Code code,
            const char (&arr)[N],
            const Mime::MediaType& mime = Mime::MediaType())
    {
        /* @Refactor: code duplication */
        code_ = code;

        if (mime.isValid()) {
            auto contentType = headers_.tryGet<Header::ContentType>();
            if (contentType)
                contentType->setMime(mime);
            else
                headers_.add(std::make_shared<Header::ContentType>(mime));
        }

        return putOnWire(arr, N - 1);
    }

    ResponseStream stream(Code code, size_t streamSize = DefaultStreamSize) {
        code_ = code;

        return ResponseStream(std::move(*this), peer_, transport_, streamSize);
    }

    // Unsafe API

    DynamicStreamBuf *rdbuf() {
       return &buf_;
    }

    DynamicStreamBuf *rdbuf(DynamicStreamBuf* other) {
       throw std::domain_error("Unimplemented");
    }

private:
    Response()
        : Message()
        , buf_(0)
        , transport_(nullptr)
    { }

    Response(Tcp::Transport* transport)
        : Message()
        , buf_(DefaultStreamSize)
        , transport_(transport)
    { }

    std::shared_ptr<Tcp::Peer> peer() const {
        if (peer_.expired())
            throw std::runtime_error("Write failed: Broken pipe");

        return peer_.lock();
    }

    template<typename Ptr>
    void associatePeer(const Ptr& peer) {
        if (peer_.use_count() > 0)
            throw std::runtime_error("A peer was already associated to the response");

        peer_ = peer;
    }

    Async::Promise<ssize_t> putOnWire(const char* data, size_t len);

    std::weak_ptr<Tcp::Peer> peer_;
    DynamicStreamBuf buf_;
    Tcp::Transport *transport_;
};

Async::Promise<ssize_t> serveFile(
        Response& response, const char *fileName,
        const Mime::MediaType& contentType = Mime::MediaType());

namespace Private {

    enum class State { Again, Next, Done };

    struct Step {
        Step(Message* request)
            : message(request)
        { }

        virtual State apply(StreamCursor& cursor) = 0;

        void raise(const char* msg, Code code = Code::Bad_Request);

        Message *message;
    };

    struct RequestLineStep : public Step {
        RequestLineStep(Request* request)
            : Step(request)
        { }

        State apply(StreamCursor& cursor);
    };

    struct ResponseLineStep : public Step {
        ResponseLineStep(Response* response)
            : Step(response)
        { }

        State apply(StreamCursor& cursor);
    };

    struct HeadersStep : public Step {
        HeadersStep(Message* request)
            : Step(request)
        { }

        State apply(StreamCursor& cursor);
    };

    struct BodyStep : public Step {
        BodyStep(Message* request)
            : Step(request)
            , bytesRead(0)
        { }

        State apply(StreamCursor& cursor);

    private:
        size_t bytesRead;
    };

    struct ParserBase {
        ParserBase()
            : currentStep(0)
            , cursor(&buffer)
        {
        }

        ParserBase(const char* data, size_t len)
            : currentStep(0)
            , cursor(&buffer)
        {
        }

        ParserBase(const ParserBase& other) = delete;
        ParserBase(ParserBase&& other) = default;

        bool feed(const char* data, size_t len);
        virtual void reset();

        State parse();

        ArrayStreamBuf<Const::MaxBuffer> buffer;
        StreamCursor cursor;

    protected:
        static constexpr size_t StepsCount = 3;

        std::array<std::unique_ptr<Step>, StepsCount> allSteps;
        size_t currentStep;

    };

    template<typename Message> struct Parser;

    template<> struct Parser<Http::Request> : public ParserBase {
        Parser()
            : ParserBase()
        { 
            allSteps[0].reset(new RequestLineStep(&request));
            allSteps[1].reset(new HeadersStep(&request));
            allSteps[2].reset(new BodyStep(&request));
        }

        Parser(const char* data, size_t len)
            : ParserBase()
        {
            allSteps[0].reset(new RequestLineStep(&request));
            allSteps[1].reset(new HeadersStep(&request));
            allSteps[2].reset(new BodyStep(&request));

            feed(data, len);
        }

        void reset() {
            ParserBase::reset();

            request.headers_.clear();
            request.body_.clear();
            request.resource_.clear();
            request.query_.clear();
        }

        Request request;
    };

    template<> struct Parser<Http::Response> : public ParserBase {
        Parser()
            : ParserBase()
        {
            allSteps[0].reset(new ResponseLineStep(&response));
            allSteps[1].reset(new HeadersStep(&response));
            allSteps[2].reset(new BodyStep(&response));
        }

        Parser(const char* data, size_t len)
            : ParserBase()
        {
            allSteps[0].reset(new ResponseLineStep(&response));
            allSteps[1].reset(new HeadersStep(&response));
            allSteps[2].reset(new BodyStep(&response));

            feed(data, len);
        }

        Response response;
    };

} // namespace Private

class Handler : public Net::Tcp::Handler {
public:
    void onInput(const char* buffer, size_t len, const std::shared_ptr<Tcp::Peer>& peer);

    void onConnection(const std::shared_ptr<Tcp::Peer>& peer);
    void onDisconnection(const std::shared_ptr<Tcp::Peer>& peer);

    virtual void onRequest(const Request& request, Response response, Timeout timeout) = 0;

    virtual void onTimeout(const Request& request, Response response);

private:
    Private::Parser<Http::Request>& getParser(const std::shared_ptr<Tcp::Peer>& peer) const;
};

} // namespace Http

} // namespace Net


