/*
 * SPDX-FileCopyrightText: 2016 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
   Mathieu Stefani, 15 juin 2016

   A lightweight implementation of the Reactor design-pattern.

   The main goal of this component is to provide an solid abstraction
   that can be used internally and by client code to dispatch I/O events
   to callbacks and handlers, in an efficient way.
*/

#pragma once

#include <pistache/flags.h>
#include <pistache/net.h>
#include <pistache/os.h>
#include <pistache/prototype.h>

#include <sys/resource.h>
#include <sys/time.h>

#include <memory>
#include <thread>
#include <vector>

namespace Pistache::Aio
{

    // A set of fds that are ready
    class FdSet
    {
    public:
        FdSet() = delete;

        explicit FdSet(std::vector<Polling::Event>&& events)
            : events_()
        {
            events_.reserve(events.size());
            events_.insert(events_.end(), std::make_move_iterator(events.begin()),
                           std::make_move_iterator(events.end()));
        }

        struct Entry : private Polling::Event
        {
            Entry(Polling::Event&& event)
                : Polling::Event(std::move(event))
            { }

            bool isReadable() const { return flags.hasFlag(Polling::NotifyOn::Read); }
            bool isWritable() const { return flags.hasFlag(Polling::NotifyOn::Write); }
            bool isHangup() const { return flags.hasFlag(Polling::NotifyOn::Hangup); }

            Polling::Tag getTag() const { return this->tag; }
        };

        using iterator       = std::vector<Entry>::iterator;
        using const_iterator = std::vector<Entry>::const_iterator;

        size_t size() const { return events_.size(); }

        const Entry& at(size_t index) const { return events_.at(index); }

        const Entry& operator[](size_t index) const { return events_.at(index); }

        iterator begin() { return events_.begin(); }

        iterator end() { return events_.end(); }

        const_iterator begin() const { return events_.begin(); }

        const_iterator end() const { return events_.end(); }

    private:
        std::vector<Entry> events_;
    };

    class Handler;
    class ExecutionContext;

    class Reactor : public std::enable_shared_from_this<Reactor>
    {
    public:
        class Impl;

        Reactor();
        ~Reactor();

        struct Key
        {

            Key();

            friend class Reactor;
            friend class Impl;
            friend class SyncImpl;
            friend class AsyncImpl;

            uint64_t data() const { return data_; }

        private:
            explicit Key(uint64_t data);
            uint64_t data_;
        };

        static std::shared_ptr<Reactor> create();

        void init();
        void init(const ExecutionContext& context);

        Key addHandler(const std::shared_ptr<Handler>& handler);

        void detachFromReactor(const std::shared_ptr<Handler>& handler);
        void detachAndRemoveAllHandlers();

        std::vector<std::shared_ptr<Handler>> handlers(const Key& key);

        void registerFd(const Key& key, Fd fd, Polling::NotifyOn interest,
                        Polling::Tag tag, Polling::Mode mode = Polling::Mode::Level);
        void registerFdOneShot(const Key& key, Fd fd, Polling::NotifyOn interest,
                               Polling::Tag tag,
                               Polling::Mode mode = Polling::Mode::Level);

        void registerFd(const Key& key, Fd fd, Polling::NotifyOn interest,
                        Polling::Mode mode = Polling::Mode::Level);
        void registerFdOneShot(const Key& key, Fd fd,
                               Polling::NotifyOn interest,
                               Polling::Mode mode = Polling::Mode::Level);

        void modifyFd(const Key& key, Fd fd, Polling::NotifyOn interest,
                      Polling::Mode mode = Polling::Mode::Level);

        void modifyFd(const Key& key, Fd fd, Polling::NotifyOn interest,
                      Polling::Tag tag, Polling::Mode mode = Polling::Mode::Level);

        void removeFd(const Key& key, Fd fd);

        void runOnce();
        void run();

        void shutdown();

    private:
        Impl* impl() const;
        std::unique_ptr<Impl> impl_;
    };

    class ExecutionContext
    {
    public:
        virtual ~ExecutionContext()                             = default;
        virtual Reactor::Impl* makeImpl(Reactor* reactor) const = 0;
    };

    class SyncContext : public ExecutionContext
    {
    public:
        ~SyncContext() override = default;
        Reactor::Impl* makeImpl(Reactor* reactor) const override;
    };

    class AsyncContext : public ExecutionContext
    {
    public:
        explicit AsyncContext(size_t threads, const std::string& threadsName = "")
            : threads_(threads)
            , threadsName_(threadsName)
        { }

        ~AsyncContext() override = default;

        Reactor::Impl* makeImpl(Reactor* reactor) const override;

        static AsyncContext singleThreaded();

    private:
        size_t threads_;
        std::string threadsName_;
    };

    class Handler : public Prototype<Handler>
    {
    public:
        friend class Reactor;
        friend class SyncImpl;
        friend class AsyncImpl;

        Handler()
            : reactor_(nullptr)
            , context_()
            , key_()
        { }

        struct Context
        {
            friend class SyncImpl;

            Context()
                : tid()
            { }

            std::thread::id thread() const { return tid; }

        private:
            std::thread::id tid;
        };

        virtual void onReady(const FdSet& fds)                = 0;
        virtual void registerPoller(Polling::Epoll& poller)   = 0;
        virtual void unregisterPoller(Polling::Epoll& poller) = 0;

        Reactor* reactor() const { return reactor_; }

        Context context() const { return context_; }

        Reactor::Key key() const { return key_; };

        ~Handler() override = default;

    private:
        // @Mar/2024. reactor_ being a raw cptr "Reactor*" caused an issue as
        // follows.
        //
        // The class Client (see client.h) holds a std::shared_ptr to
        // Reactor. In certain cases, the Reactor destructor was being invoked
        // automatically out of Client destructor. However, the class Transport
        // was also holding a raw cptr Reactor *, inherited from
        // Aio::Handler. In Transport::removePeer, the code invokes
        // reactor()->removeFd(); this invocation was happening AFTER the
        // Reactor destructor had already been called.
        //
        // In macOS, that was causing an exception because Reactor::impl_ was
        // null when removeFd() was called. In Linux, removeFd() appeared to
        // work because the memory of the Reactor instance had (by good
        // fortune) not yet been overwritten.
        //
        // However, even in Linux the problem could be demonstrated by
        // providing a destructor for Reactor as follows:
        //   Remove this line from reactor.cc:
        //     Reactor::~Reactor() = default;
        //   Replace with this line:
        //     Reactor::~Reactor() {impl_ = NULL;}//impl_ is member of Reactor_
        // With this change, the test program run_http_server_test was creating
        // the same exception in macOS and Linux
        //
        // Nonetheless, we can't make reactor_ a shared_ptr without a great
        // many other code changes. If we did, then Handler would hold a
        // std::shared_ptr<Reactor>; and Reactor would hold a
        // std::vector<std::shared_ptr<Handler>>. Since then each of
        // Reactor and Handler would be holding a shared_ptr to the other,
        // neither could ever go out of scope and exit.
        //
        // Instead, in the Reactor destructor we invoke a new method,
        // detachAndRemoveAllHandlers. This deregisters the Reactor from each
        // Handler, and wipes away Reactor's "handlers" vector. Subsequently,
        // Transport::removePeer will no longer try to remove an Fd from the
        // Reactor, because Transport will no longer have a registered Reactor.
        //
        // Finally, we've added std::mutex reg_unreg_mutex_ to class Epoll
        // (each poller is an instance of Epoll). This mutex is claimed when
        // invoking unregisterPoller; it is also claimed for each ieration of
        // the polling loops defined in Listener::run and Reactor::runOnce, so
        // that Handlers cannot be removed, registration-related file handles
        // closed, etc., in the middle of gathering and processing polled
        // events.
        Reactor* reactor_;
        Context context_;
        Reactor::Key key_;
    };

} // namespace Pistache::Aio
