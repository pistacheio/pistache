/* 
   Mathieu Stefani, 15 juin 2016
   
   A lightweight implementation of the Reactor design-pattern.

   The main goal of this component is to provide an solid abstraction
   that can be used internally and by client code to dispatch I/O events
   to callbacks and handlers, in an efficient way.
*/

#pragma once

#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <unordered_map>

#include <sys/time.h>
#include <sys/resource.h>

#include <pistache/flags.h>
#include <pistache/os.h>
#include <pistache/net.h>
#include <pistache/prototype.h>


namespace Pistache {
namespace Aio {

// A set of fds that are ready
class FdSet {
public:
    FdSet(std::vector<Polling::Event>&& events)
    {
        events_.reserve(events.size());
        for (auto &&event: events) {
            events_.push_back(std::move(event));
        }
    }

    struct Entry : private Polling::Event {
        Entry(Polling::Event&& event)
            : Polling::Event(std::move(event))
        { }

        bool isReadable() const {
            return flags.hasFlag(Polling::NotifyOn::Read);
        }
        bool isWritable() const {
            return flags.hasFlag(Polling::NotifyOn::Write);
        }
        bool isHangup() const {
            return flags.hasFlag(Polling::NotifyOn::Hangup);
        }

        Fd getFd() const { return this->fd; }
        Polling::Tag getTag() const { return this->tag; }
    };

    typedef std::vector<Entry>::iterator iterator;
    typedef std::vector<Entry>::const_iterator const_iterator;

    size_t size() const {
        return events_.size();
    }

    const Entry& at(size_t index) const {
        return events_.at(index);
    }

    const Entry& operator[](size_t index) const {
        return events_[index];
    }

    iterator begin() {
        return events_.begin();
    }

    iterator end() {
        return events_.end();
    }

    const_iterator begin() const {
        return events_.begin();
    }

    const_iterator end() const {
        return events_.end();
    }

private:
    std::vector<Entry> events_;
};

class Handler;
class ExecutionContext;

class Reactor : public std::enable_shared_from_this<Reactor> {
public:

    class Impl;

    Reactor();
    ~Reactor();

    struct Key {

        Key();

        friend class Reactor;
        friend class Impl;
        friend class SyncImpl;
        friend class AsyncImpl;

        uint64_t data() const {
            return data_;
        }

    private:
        Key(uint64_t data);
        uint64_t data_;
    };


    static std::shared_ptr<Reactor> create();

    void init();
    void init(const ExecutionContext& context);

    Key addHandler(const std::shared_ptr<Handler>& handler);

    std::vector<std::shared_ptr<Handler>> handlers(const Key& key);

    void registerFd(
            const Key& key, Fd fd, Polling::NotifyOn interest, Polling::Tag tag,
            Polling::Mode mode = Polling::Mode::Level);
    void registerFdOneShot(
            const Key& key, Fd fd, Polling::NotifyOn intereset, Polling::Tag tag,
            Polling::Mode mode = Polling::Mode::Level);

    void registerFd(
            const Key& key, Fd fd, Polling::NotifyOn interest,
            Polling::Mode mode = Polling::Mode::Level);
    void registerFdOneShot(
            const Key& key, Fd fd, Polling::NotifyOn interest,
            Polling::Mode mode = Polling::Mode::Level);

    void modifyFd(
            const Key& key, Fd fd, Polling::NotifyOn interest,
            Polling::Mode mode = Polling::Mode::Level);

    void modifyFd(
            const Key& key, Fd fd, Polling::NotifyOn interest, Polling::Tag tag, 
            Polling::Mode mode = Polling::Mode::Level);

    void runOnce();
    void run();

    void shutdown();

private:
    Impl* impl() const;
    std::unique_ptr<Impl> impl_;
};

class ExecutionContext {
public:
    virtual Reactor::Impl* makeImpl(Reactor* reactor) const = 0;
};

class SyncContext : public ExecutionContext {
public:
    Reactor::Impl* makeImpl(Reactor* reactor) const;
};

class AsyncContext : public ExecutionContext {
public:
    AsyncContext(size_t threads)
        : threads_(threads)
    { }

    Reactor::Impl* makeImpl(Reactor* reactor) const;

    static AsyncContext singleThreaded();

private:
    size_t threads_;
};

class Handler : public Prototype<Handler> {
public:
    friend class Reactor;
    friend class SyncImpl;
    friend class AsyncImpl;

    struct Context {
        friend class SyncImpl;

        std::thread::id thread() const { return tid; }

    private:
        std::thread::id tid;
    };

    virtual void onReady(const FdSet& fds) = 0;
    virtual void registerPoller(Polling::Epoll& poller) { }

    Reactor* reactor() const {
        return reactor_;
    }

    Context context() const {
        return context_;
    }

    Reactor::Key key() const {
        return key_;
    };

private:
    Reactor* reactor_;
    Context context_;
    Reactor::Key key_;
};

} // namespace Aio
} // namespace Pistache
