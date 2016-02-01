/* 
   Mathieu Stefani, 29 janvier 2016
   
   The Http client
*/

#pragma once
#include "async.h" 
#include "os.h"
#include "http.h"
#include "io.h"
#include <atomic>
#include <sys/types.h>
#include <sys/socket.h>
#include <deque>

namespace Net {

namespace Http {

class Connection;

class Transport : public Io::Handler {
public:
    PROTOTYPE_OF(Io::Handler, Transport)

    void onReady(const Io::FdSet& fds);
    void registerPoller(Polling::Epoll& poller);

    Async::Promise<void>
    asyncConnect(Fd fd, const struct sockaddr* address, socklen_t addr_len);

    template<typename Buf>
    Async::Promise<ssize_t> asyncWrite(Fd fd, const Buf& buffer, int flags = 0) {
        // If the I/O operation has been initiated from an other thread, we queue it and we'll process
        // it in our own thread so that we make sure that every I/O operation happens in the right thread
        if (std::this_thread::get_id() != io()->thread()) {
            return Async::Promise<ssize_t>([=](Async::Resolver& resolve, Async::Rejection& reject) {
                BufferHolder holder(buffer);
                auto detached = holder.detach();
                OnHoldWrite write(std::move(resolve), std::move(reject), detached, flags);
                write.peerFd = fd;
                auto *e = writesQueue.allocEntry(std::move(write));
                writesQueue.push(e);
            });
        }
        return Async::Promise<ssize_t>([&](Async::Resolver& resolve, Async::Rejection& reject) {

            auto it = toWrite.find(fd);
            if (it != std::end(toWrite)) {
                reject(Net::Error("Multiple writes on the same fd"));
                return;
            }

            asyncWriteImpl(fd, flags, BufferHolder(buffer), std::move(resolve), std::move(reject));

        });
    }

    void addInFlight(Fd fd, Async::Resolver&& resolve, Async::Rejection&& reject) {
        InflightRequest req(std::move(resolve), std::move(reject));
        inflightRequests.insert(
                std::make_pair(fd, std::move(req)));
    }

private:

    struct Event {
        enum class Type {
            Connection
        };

        virtual Type type() const = 0;
    };

    struct ConnectionEvent : public Event {
        ConnectionEvent(const Connection* connection);

        Type type() const { return Type::Connection; }

        const Connection* connection_;
    };

    enum WriteStatus {
        FirstTry,
        Retry
    };

    struct BufferHolder {
        enum Type { Raw, File };

        explicit BufferHolder(const Buffer& buffer)
            : type(Raw)
            , u(buffer)
        {
            size_ = buffer.len;
        }

        explicit BufferHolder(const FileBuffer& buffer)
            : type(File)
            , u(buffer.fd())
        {
            size_ = buffer.size();
        }

        bool isFile() const { return type == File; }
        bool isRaw() const { return type == Raw; }
        size_t size() const { return size_; }

        Fd fd() const {
            if (!isFile())
                throw std::runtime_error("Tried to retrieve fd of a non-filebuffer");

            return u.fd;

        }

        Buffer raw() const {
            if (!isRaw())
                throw std::runtime_error("Tried to retrieve raw data of a non-buffer");

            return u.raw;
        }

        BufferHolder detach(size_t offset = 0) const {
            if (!isRaw())
                return BufferHolder(u.fd, size_);

            if (u.raw.isOwned)
                return BufferHolder(u.raw);

            auto detached = u.raw.detach(offset);
            return BufferHolder(detached);
        }

    private:
        BufferHolder(Fd fd, size_t size)
         : u(fd)
         , size_(size)
         , type(File)
        { }

        union U {
            Buffer raw;
            Fd fd;

            U(Buffer buffer) : raw(buffer) { }
            U(Fd fd) : fd(fd) { }
        } u;
        size_t size_;
        Type type;
    };

    struct OnHoldWrite {
        OnHoldWrite(Async::Resolver resolve, Async::Rejection reject,
                    BufferHolder buffer, int flags = 0)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
            , buffer(std::move(buffer))
            , flags(flags)
            , peerFd(-1)
        { }

        Async::Resolver resolve;
        Async::Rejection reject;
        BufferHolder buffer;
        int flags;
        Fd peerFd;
    };

    struct PendingConnection {
        PendingConnection(
                Async::Resolver resolve, Async::Rejection reject,
                Fd fd, const struct sockaddr* addr, socklen_t addr_len)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
            , fd(fd)
            , addr(addr)
            , addr_len(addr_len)
        { }

        Async::Resolver resolve;
        Async::Rejection reject;
        Fd fd;
        const struct sockaddr* addr;
        socklen_t addr_len;
    };

    struct InflightRequest {
        InflightRequest(
                Async::Resolver resolve, Async::Rejection reject)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
        {
            parser.reset(new Private::Parser<Http::Response>());
        }

        Async::Resolver resolve;
        Async::Rejection reject;
        std::shared_ptr<Private::Parser<Http::Response>> parser;
    };


    PollableQueue<OnHoldWrite> writesQueue;
    PollableQueue<PendingConnection> connectionsQueue;

    std::unordered_map<Fd, OnHoldWrite> toWrite;
    std::unordered_map<Fd, PendingConnection> pendingConnections;
    std::unordered_map<Fd, InflightRequest> inflightRequests;

    void asyncWriteImpl(Fd fd, OnHoldWrite& entry, WriteStatus status = FirstTry);
    void asyncWriteImpl(
            Fd fd, int flags, const BufferHolder& buffer,
            Async::Resolver resolve, Async::Rejection reject,
            WriteStatus status = FirstTry);
    void handleWriteQueue();
    void handleConnectionQueue();
    void handleIncoming(Fd fd);
    void handleResponsePacket(Fd fd, const char* buffer, size_t totalBytes);


};

class ConnectionPool;

struct Connection {

    friend class ConnectionPool;

    enum State : uint32_t {
        Idle,
        Used,
        Connecting,
        Connected
    };

    Connection()
        : fd(-1)
    {
    }

    void connect(Net::Address addr);
    bool isConnected() const;
    bool hasTransport() const;
    void associateTransport(const std::shared_ptr<Transport>& transport);

    Async::Promise<Response> perform(const Http::Request& request);

    Fd fd;

private:
    void performImpl(
            const Http::Request& request,
            Async::Resolver resolve,
            Async::Rejection reject);
    void processRequestQueue();

    struct RequestData {
        RequestData(Async::Resolver resolve, Async::Rejection reject, const Http::Request& request)
            : resolve(std::move(resolve))
            , reject(std::move(reject))
            , request(request)
        { }
        Async::Resolver resolve;
        Async::Rejection reject;

        Http::Request request;
    };

    std::atomic<uint32_t> state_;
    std::shared_ptr<Transport> transport_;
    Queue<RequestData> requestsQueue;
};

struct ConnectionPool {
    void init(size_t max = 1);

    std::shared_ptr<Connection> pickConnection();
    void returnConnection(const std::shared_ptr<Connection>& connection);

private:
    std::vector<std::shared_ptr<Connection>> connections;
};

namespace Default {
    constexpr int Threads = 1;
    constexpr int MaxConnections = 8;
    constexpr bool KeepAlive = true;
}

class Client {
public:

   struct Options {
       friend class Client;

       Options()
           : threads_(Default::Threads)
           , maxConnections_(Default::MaxConnections)
           , keepAlive_(Default::KeepAlive)

       { }

       Options& threads(int val);
       Options& maxConnections(int val);
       Options& keepAlive(bool val);
   private:
       int threads_;
       int maxConnections_;
       bool keepAlive_;
   };

   Client(const std::string &base);

   static Options options();
   void init(const Options& options);

   RequestBuilder request(std::string resource);

   Async::Promise<Response> get(
           const Http::Request& request,
           std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

private:
   Io::ServiceGroup io_;
   std::string url_;
   Net::Address addr_;

   std::shared_ptr<Transport> transport_;
   ConnectionPool pool;
   std::atomic<uint64_t> ioIndex;

};

} // namespace Http

} // namespace Net

