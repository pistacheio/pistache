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
    Version version_;

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

// 6. Response
class Response : private Message {
public:
    friend class Handler;

    Response(const Response& other) = delete;
    Response& operator=(const Response& other) = delete;

    // C++11: std::weak_ptr move constructor is C++14 only so the default
    // version of move constructor / assignement operator does not work and we
    // have to define it ourself
    Response(Response&& other);
    Response& operator=(Response&& other);

    Header::Collection& headers();
    const Header::Collection& headers() const;

    void setMime(const Mime::MediaType& mime);

    Async::Promise<ssize_t> send(Code code);
    Async::Promise<ssize_t> send(Code code, const std::string& body, const Mime::MediaType &mime = Mime::MediaType());

private:
    Response();

    std::shared_ptr<Tcp::Peer> peer() const;

    void associatePeer(const std::shared_ptr<Tcp::Peer>& peer);
    std::weak_ptr<Tcp::Peer> peer_;

    size_t bufSize_;
    std::unique_ptr<char[]> buffer_;

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


