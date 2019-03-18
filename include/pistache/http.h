/* http.h
   Mathieu Stefani, 13 August 2015

   Http Layer
*/

#pragma once

#include <type_traits>
#include <stdexcept>
#include <array>
#include <vector>
#include <sstream>
#include <algorithm>
#include <memory>
#include <string>

#include <sys/timerfd.h>

#include <pistache/net.h>
#include <pistache/http_headers.h>
#include <pistache/http_defs.h>
#include <pistache/cookie.h>
#include <pistache/stream.h>
#include <pistache/mime.h>
#include <pistache/async.h>
#include <pistache/peer.h>
#include <pistache/tcp.h>
#include <pistache/transport.h>
#include <pistache/view.h>

namespace Pistache {
namespace Http {

namespace details {
    struct prototype_tag { };

    template<typename P>
    struct IsHttpPrototype {
        template<typename U> static auto test(U *) -> decltype(typename U::tag());
        template<typename U> static auto test(...) -> std::false_type;

        static constexpr bool value =
            std::is_same<decltype(test<P>(nullptr)), prototype_tag>::value;
    };
}

#define HTTP_PROTOTYPE(Class) \
    PROTOTYPE_OF(Pistache::Tcp::Handler, Class) \
    typedef Pistache::Http::details::prototype_tag tag;

namespace Private {
    class ParserBase;
    template<typename T> class Parser;
    class RequestLineStep;
    class ResponseLineStep;
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
    friend class Private::HeadersStep;
    friend class Private::BodyStep;
    friend class Private::ParserBase;

    Message();

    Message(const Message& other) = default;
    Message& operator=(const Message& other) = default;

    Message(Message&& other) = default;
    Message& operator=(Message&& other) = default;

protected:
    Version version_;
    Code code_;

    std::string body_;

    CookieJar cookies_;
    Header::Collection headers_;

};

namespace Uri {

    class Query {
    public:
        Query();
        Query(std::initializer_list<std::pair<const std::string, std::string>> params);

        void add(std::string name, std::string value);
        Optional<std::string> get(const std::string& name) const;
        bool has(const std::string& name) const;
        // Return empty string or "?key1=value1&key2=value2" if query exist
        std::string as_str() const;

        void clear() {
            params.clear();
        }

        // \brief Return iterator to the beginning of the parameters map
        std::unordered_map<std::string, std::string>::const_iterator
          parameters_begin() const {
            return params.begin();
        }

        // \brief Return iterator to the end of the parameters map
        std::unordered_map<std::string, std::string>::const_iterator
          parameters_end() const {
            return params.end();
        }

        // \brief returns all parameters given in the query
        std::vector<std::string> parameters() const {
          std::vector<std::string> keys;
          std::transform(params.begin(), params.end(), std::back_inserter(keys),
            [](const std::unordered_map<std::string, std::string>::value_type
               &pair) {return pair.first;});
          return keys;
        }

    private:
        //first is key second is value
        std::unordered_map<std::string, std::string> params;
    };
} // namespace Uri


// 5. Request
class Request : public Message {
public:
    friend class Private::RequestLineStep;
    friend class Private::Parser<Http::Request>;

    friend class RequestBuilder;
    // @Todo: try to remove the need for friend-ness here
    friend class Client;

    Request();

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

class Handler;
class ResponseWriter;

class Timeout {
public:

    friend class ResponseWriter;

    Timeout(Timeout&& other)
        : handler(other.handler)
        , request(std::move(other.request))
        , transport(other.transport)
        , armed(other.armed)
        , timerFd(other.timerFd)
        , peer(std::move(other.peer))
    {
        other.timerFd = -1;
    }

    Timeout& operator=(Timeout&& other) {
        handler = other.handler;
        transport = other.transport;
        request = std::move(other.request);
        armed = other.armed;
        timerFd = other.timerFd;
        other.timerFd = -1;
        peer = std::move(other.peer);
        return *this;
    }

    template<typename Duration>
    void arm(Duration duration) {
        Async::Promise<uint64_t> p([=](Async::Deferred<uint64_t> deferred) {
            timerFd = TRY_RET(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));
            transport->armTimer(timerFd, duration, std::move(deferred));
        });

        p.then(
            [=](uint64_t numWakeup) {
                this->armed = false;
                this->onTimeout(numWakeup);
                close(timerFd);
        },
        [=](std::exception_ptr exc) {
            std::rethrow_exception(exc);
        });

        armed = true;
    }

    void disarm() {
        if (armed) {
            transport->disarmTimer(timerFd);
        }
    }

    bool isArmed() const {
        return armed;
    }

private:
    Timeout(const Timeout& other)
        : handler(other.handler)
        , request(other.request)
        , transport(other.transport)
        , armed(other.armed)
        , timerFd(other.timerFd)
        , peer()
    { }

    Timeout(Tcp::Transport* transport_,
            Handler* handler_,
            Request request_)
        : handler(handler_)
        , request(std::move(request_))
        , transport(transport_)
        , armed(false)
        , timerFd(-1)
        , peer()
    { }

    template<typename Ptr>
    void associatePeer(const Ptr& ptr) {
        peer = ptr;
    }

    void onTimeout(uint64_t numWakeup);

    Handler* handler;
    Request request;
    Tcp::Transport* transport;
    bool armed;
    Fd timerFd;
    std::weak_ptr<Tcp::Peer> peer;
};

class ResponseStream : public Message {
public:
    friend class ResponseWriter;

    ResponseStream(ResponseStream&& other)
        : Message(std::move(other))
        , peer_(std::move(other.peer_))
        , buf_(std::move(other.buf_))
        , transport_(other.transport_)
        , timeout_(std::move(other.timeout_))
    { }

    ResponseStream& operator=(ResponseStream&& other) {
        Message::operator=(std::move(other));
        peer_ = std::move(other.peer_);
        buf_ = std::move(other.buf_);
        transport_ = other.transport_;
        timeout_ = std::move(other.timeout_);

        return *this;
    }

    template<typename T>
    friend
    ResponseStream& operator<<(ResponseStream& stream, const T& val);

    std::streamsize write(const char * data, std::streamsize sz) {
        std::ostream os(&buf_);
        os << std::hex << sz << crlf;
        os.write(data, sz);
        os << crlf;
        return sz;
    }

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
            Timeout timeout,
            size_t streamSize);

    std::shared_ptr<Tcp::Peer> peer() const {
        if (peer_.expired())
            throw std::runtime_error("Write failed: Broken pipe");

        return peer_.lock();
    }

    std::weak_ptr<Tcp::Peer> peer_;
    DynamicStreamBuf buf_;
    Tcp::Transport* transport_;
    Timeout timeout_;
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
    Size<T> size;

    std::ostream os(&stream.buf_);
    os << std::hex << size(val) << crlf;
    os << val << crlf;

    return stream;
}

inline ResponseStream&
operator<<(ResponseStream& stream, ResponseStream & (*func)(ResponseStream &)) {
    return (*func)(stream);
}

// 6. Response
// @Investigate public inheritence
class Response : public Message {
public:
    friend class Private::ResponseLineStep;
    friend class Private::Parser<Http::Response>;

    Response() = default;

    explicit Response(Version version)
        : Message()
    {
        version_ = version;
    }

    Response(const Response& other) = default;
    Response& operator=(const Response& other) = default;
    Response(Response&& other) = default;
    Response& operator=(Response&& other) = default;

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

    std::string body() const {
        return body_;
    }

    Version version() const {
        return version_;
    }

};

class ResponseWriter : public Response {
public:
    static constexpr size_t DefaultStreamSize = 512;

    friend Async::Promise<ssize_t> serveFile(ResponseWriter&, const std::string&, const Mime::MediaType&);

    friend class Handler;
    friend class Timeout;

    ResponseWriter& operator=(const ResponseWriter& other) = delete;

    friend class Private::ResponseLineStep;
    friend class Private::Parser<Http::Response>;

    //
    // C++11: std::weak_ptr move constructor is C++14 only so the default
    // version of move constructor / assignement operator does not work and we
    // have to define it ourself
    ResponseWriter(ResponseWriter&& other)
        : Response(std::move(other))
        , peer_(other.peer_)
        , buf_(std::move(other.buf_))
        , transport_(other.transport_)
        , timeout_(std::move(other.timeout_))
    { }
    ResponseWriter& operator=(ResponseWriter&& other) {
        Response::operator=(std::move(other));
        peer_ = std::move(other.peer_);
        transport_ = other.transport_;
        buf_ = std::move(other.buf_);
        timeout_ = std::move(other.timeout_);
        return *this;
    }

    void setMime(const Mime::MediaType& mime) {
        auto ct = headers_.tryGet<Header::ContentType>();
        if (ct)
            ct->setMime(mime);
        else
            headers_.add(std::make_shared<Header::ContentType>(mime));
    }

    /* @Feature: add helper functions for common http return code:
     * - halt() -> 404
     * - movedPermantly -> 301
     * - moved() -> 302
     */

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

        return ResponseStream(
                std::move(*this), peer_, transport_, std::move(timeout_), streamSize);
    }

    template<typename Duration>
    void timeoutAfter(Duration duration) {
        timeout_.arm(duration);
    }

    Timeout& timeout() {
        return timeout_;
    }

    std::shared_ptr<Tcp::Peer> peer() const {
        if (peer_.expired())
            throw std::runtime_error("Write failed: Broken pipe");

        return peer_.lock();
    }

    // Unsafe API

    DynamicStreamBuf *rdbuf() {
       return &buf_;
    }

    DynamicStreamBuf *rdbuf(DynamicStreamBuf* other) {
       UNUSED(other)
       throw std::domain_error("Unimplemented");
    }

    ResponseWriter clone() const {
        return ResponseWriter(*this);
    }

private:
    ResponseWriter(Tcp::Transport* transport, Request request, Handler* handler)
        : Response(request.version())
        , peer_()
        , buf_(DefaultStreamSize)
        , transport_(transport)
        , timeout_(transport, handler, std::move(request))
    { }

    ResponseWriter(const ResponseWriter& other)
        : Response(other)
        , peer_(other.peer_)
        , buf_(DefaultStreamSize)
        , transport_(other.transport_)
        , timeout_(other.timeout_)
    { }

    template<typename Ptr>
    void associatePeer(const Ptr& peer) {
        if (peer_.use_count() > 0)
            throw std::runtime_error("A peer was already associated to the response");

        peer_ = peer;
        timeout_.associatePeer(peer_);
    }

    Async::Promise<ssize_t> putOnWire(const char* data, size_t len);

    std::weak_ptr<Tcp::Peer> peer_;
    DynamicStreamBuf buf_;
    Tcp::Transport *transport_;
    Timeout timeout_;
};

Async::Promise<ssize_t> serveFile(
        ResponseWriter& response, const std::string& fileName,
        const Mime::MediaType& contentType = Mime::MediaType());

namespace Private {

    enum class State { Again, Next, Done };

    struct Step {
        explicit Step(Message* request)
            : message(request)
        { }

        virtual ~Step() = default;

        virtual State apply(StreamCursor& cursor) = 0;

        static void raise(const char* msg, Code code = Code::Bad_Request);

        Message *message;
    };

    class RequestLineStep : public Step {
    public:
        explicit RequestLineStep(Request* request)
            : Step(request)
        { }

        State apply(StreamCursor& cursor) override;
    };

    class ResponseLineStep : public Step {
    public:
        explicit ResponseLineStep(Response* response)
            : Step(response)
        { }

        State apply(StreamCursor& cursor) override;
    };

    class HeadersStep : public Step {
    public:
        explicit HeadersStep(Message* request)
            : Step(request)
        { }

        State apply(StreamCursor& cursor) override;
    };

    class BodyStep : public Step {
    public:
        explicit BodyStep(Message* message_)
            : Step(message_)
            , chunk(message_)
            , bytesRead(0)
        { }

        State apply(StreamCursor& cursor) override;

    private:
        struct Chunk {
            enum Result { Complete, Incomplete, Final };

            explicit Chunk(Message* message_)
              : message(message_)
              , bytesRead(0)
              , size(-1)
            { }

            Result parse(StreamCursor& cursor);

            void reset() {
                bytesRead = 0;
                size = -1;
            }

        private:
            Message* message;
            size_t bytesRead;
            ssize_t size;
        };

        State parseContentLength(StreamCursor& cursor, const std::shared_ptr<Header::ContentLength>& cl);
        State parseTransferEncoding(StreamCursor& cursor, const std::shared_ptr<Header::TransferEncoding>& te);

        Chunk chunk;
        size_t bytesRead;
    };

    class ParserBase {
    public:
        ParserBase()
            : buffer()
            , cursor(&buffer)
            , allSteps()
            , currentStep(0)
        { }

        ParserBase(const ParserBase& other) = delete;
        ParserBase(ParserBase&& other) = default;

        bool feed(const char* data, size_t len);
        virtual void reset();

        virtual ~ParserBase() { }

        State parse();

        ArrayStreamBuf<char> buffer;
        StreamCursor cursor;

    protected:
        static constexpr size_t StepsCount = 3;

        std::array<std::unique_ptr<Step>, StepsCount> allSteps;
        size_t currentStep;

    };

    template<typename Message> class Parser;

    template<> class Parser<Http::Request> : public ParserBase {

    public:

        Parser()
            : ParserBase()
            , request()
        {
            allSteps[0].reset(new RequestLineStep(&request));
            allSteps[1].reset(new HeadersStep(&request));
            allSteps[2].reset(new BodyStep(&request));
        }

        Parser(const char* data, size_t len)
            : ParserBase()
            , request()
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

    template<> class Parser<Http::Response> : public ParserBase {
    public:
        Parser()
            : ParserBase()
            , response()
        {
            allSteps[0].reset(new ResponseLineStep(&response));
            allSteps[1].reset(new HeadersStep(&response));
            allSteps[2].reset(new BodyStep(&response));
        }

        Parser(const char* data, size_t len)
            : ParserBase()
            , response()
        {
            allSteps[0].reset(new ResponseLineStep(&response));
            allSteps[1].reset(new HeadersStep(&response));
            allSteps[2].reset(new BodyStep(&response));

            feed(data, len);
        }

        Response response;
    };

} // namespace Private

class Handler : public Tcp::Handler {
public:
    void onInput(const char* buffer, size_t len, const std::shared_ptr<Tcp::Peer>& peer);

    void onConnection(const std::shared_ptr<Tcp::Peer>& peer);
    void onDisconnection(const std::shared_ptr<Tcp::Peer>& peer);

    virtual void onRequest(const Request& request, ResponseWriter response) = 0;

    virtual void onTimeout(const Request& request, ResponseWriter response);

    virtual ~Handler() { }

private:
    Private::Parser<Http::Request>& getParser(const std::shared_ptr<Tcp::Peer>& peer) const;
};

template<typename H, typename... Args>
std::shared_ptr<H> make_handler(Args&& ...args) {
    static_assert(std::is_base_of<Handler, H>::value, "An http handler must inherit from the Http::Handler class");
    static_assert(details::IsHttpPrototype<H>::value, "An http handler must be an http prototype, did you forget the HTTP_PROTOTYPE macro ?");

    return std::make_shared<H>(std::forward<Args>(args)...);
}

namespace helpers
{
    inline Address httpAddr(const StringView& view) {
        auto const str = view.toString();
        return Address(str);
        }
} // namespace helpers
} // namespace Http
} // namespace Pistache
