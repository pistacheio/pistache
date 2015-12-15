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

    friend class Handler;

    Version version() const;
    Method method() const;
    std::string resource() const;

    std::string body() const;

    const Header::Collection& headers() const;
    const Uri::Query& query() const;

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

    std::weak_ptr<Tcp::Peer> peer_;
};

class Handler;
class Timeout;
class Response;

class Timeout {
public:

    friend class Handler;

    template<typename Duration>
    void arm(Duration duration) {
        Async::Promise<uint64_t> p([=](Async::Resolver& resolve, Async::Rejection& reject) {
            io->armTimer(duration, resolve, reject);
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
        io->disarmTimer();
        armed = false;
    }

    bool isArmed() const {
        return armed;
    }

private:
    Timeout(Tcp::IoWorker* io,
            Handler* handler,
            const std::shared_ptr<Tcp::Peer>& peer,
            const Request& request)
        : io(io)
        , handler(handler)
        , peer(peer)
        , request(request)
        , armed(false)
    { }

    void onTimeout(uint64_t numWakeup);

    Handler* handler;
    std::weak_ptr<Tcp::Peer> peer;
    Request request;

    Tcp::IoWorker *io;
    bool armed;
};

class ResponseStream : private Message {
public:
    friend class Response;

    ResponseStream(ResponseStream&& other)
        : Message(std::move(other))
        , peer_(std::move(other.peer_))
        , buf_(std::move(other.buf_))
        , io_(other.io_)
    { }

    ResponseStream& operator=(ResponseStream&& other) {
        Message::operator=(std::move(other));
        peer_ = std::move(other.peer_);
        buf_ = std::move(other.buf_);
        io_ = other.io_;

        return *this;
    }

    template<typename T>
    friend
    ResponseStream& operator<<(ResponseStream& stream, const T& val);

    const Header::Collection& headers() const {
        return headers_;
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
            Tcp::IoWorker* io,
            size_t streamSize)
        : Message(std::move(other))
        , peer_(std::move(peer))
        , buf_(streamSize)
        , io_(io)
    {
        writeStatusLine();
        writeHeaders();
    }

    void writeStatusLine();
    void writeHeaders();

    std::shared_ptr<Tcp::Peer> peer() const {
        if (peer_.expired())
            throw std::runtime_error("Write failed: Broken pipe");

        return peer_.lock();
    }

    std::weak_ptr<Tcp::Peer> peer_;
    DynamicStreamBuf buf_;
    Tcp::IoWorker* io_;
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
class Response : private Message {
public:
    friend class Handler;
    friend class Timeout;

    static constexpr size_t DefaultStreamSize = 512;

    // C++11: std::weak_ptr move constructor is C++14 only so the default
    // version of move constructor / assignement operator does not work and we
    // have to define it ourself
    Response(Response&& other)
        : Message(std::move(other))
        , peer_(other.peer_)
        , buf_(std::move(other.buf_))
        , io_(other.io_)
    { }
    Response& operator=(Response&& other) {
        Message::operator=(std::move(other));
        peer_ = std::move(other.peer_);
        io_ = other.io_;
        buf_ = std::move(other.buf_);
        return *this;
    }

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

        return ResponseStream(std::move(*this), peer_, io_, streamSize);
    }


private:
    Response(Tcp::IoWorker* io)
        : Message()
        , buf_(DefaultStreamSize)
        , io_(io)
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
    Tcp::IoWorker *io_;
};


namespace Private {

    enum class State { Again, Next, Done };

    struct Step {
        Step(Request* request)
            : request(request)
        { }

        virtual State apply(StreamCursor& cursor) = 0;

        void raise(const char* msg, Code code = Code::Bad_Request);
        void raiseF(Code code, const char *msg, ...);

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

    virtual void onRequest(const Request& request, Response response, Timeout timeout) = 0;

    virtual void onTimeout(const Request& request, Response response);

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

    bool isBound() const {
        return listener.isBound();
    }

    Async::Promise<Tcp::Listener::Load> requestLoad(const Tcp::Listener::Load& old);

    static Options options();

private:
    std::shared_ptr<Handler> handler_;
    Net::Tcp::Listener listener;
};

} // namespace Http

} // namespace Net


