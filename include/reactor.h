/* 
   Mathieu Stefani, 15 juin 2016
   
   A lightweight implementation of the Reactor design-pattern.

   The main goal of this component is to provide an solid abstraction
   that can be used internally and by client code to dispatch I/O events
   to callbacks and handlers, in an efficient way.
*/

#pragma once

#include "flags.h"
#include "os.h"
#include "net.h"
#include "prototype.h"

#include <thread>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <sys/time.h>
#include <sys/resource.h>
#include <atomic>

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
class Context;

class Reactor : public std::enable_shared_from_this<Reactor> {
public:

    class Impl;

    Reactor();

    struct Key {

        friend class Reactor;
        friend class Impl;
        friend class SyncImpl;
        friend class AsyncImpl;

        std::shared_ptr<Handler> handler() const {
            return handler_;
        }

        void registerFd(
                Fd fd, Polling::NotifyOn interest, Polling::Tag tag,
                Polling::Mode mode = Polling::Mode::Level);
        void registerFdOneShot(
                Fd fd, Polling::NotifyOn interest, Polling::Tag tag,
                Polling::Mode mode = Polling::Mode::Level);

        size_t index() const {
            return index_;
        }

    private:
        Key();
        Key(size_t index, const std::shared_ptr<Handler>& handler);
        size_t index_;
        std::shared_ptr<Handler> handler_;
    };


    static std::shared_ptr<Reactor> create();

    void init();
    void init(const Context& context);

    Key addHandler(const std::shared_ptr<Handler>& handler);

    void registerFd(
            const Key& key, Fd fd, Polling::NotifyOn interest, Polling::Tag tag,
            Polling::Mode mode = Polling::Mode::Level);
    void registerFdOneShot(
            const Key& key, Fd fd, Polling::NotifyOn intereset, Polling::Tag tag,
            Polling::Mode mode = Polling::Mode::Level);

    void runOnce();
    void run();

    void shutdown();

private:
    Impl* impl() const;

    std::unique_ptr<Impl> impl_;

};

class Context {
public:
    virtual Reactor::Impl* makeImpl(const Reactor* reactor) const = 0;
};

class SyncContext : public Context {
public:
    Reactor::Impl* makeImpl(const Reactor* reactor) const;
};

class AsyncContext : public Context {
public:
    AsyncContext(size_t threads)
        : threads_(threads)
    { }

    Reactor::Impl* makeImpl(const Reactor* reactor) const;

    static AsyncContext singleThreaded();

private:
    size_t threads_;
};

class Handler : public Prototype<Handler> {
public:
    friend class Reactor;

    virtual void onReady(const FdSet& fds) = 0;
    virtual void registerPoller(Polling::Epoll& poller) { }

    Reactor* reactor() const {
        return reactor_;
    }

private:
    Reactor* reactor_;
};

} // namespace Aio
