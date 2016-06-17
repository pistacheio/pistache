/* 
   Mathieu Stefani, 15 juin 2016
   
   Implementation of the Reactor
*/

#include "reactor.h"

namespace Aio {

struct Reactor::Impl {

    Impl(Reactor* reactor)
        : reactor_(reactor)
    { }

    virtual ~Impl() = default;

    virtual Reactor::Key addHandler(
            const std::shared_ptr<Handler>& handler) = 0;

    virtual std::shared_ptr<Handler> handler(
            const Reactor::Key& key, Fd fd) const = 0;

    virtual void registerFd(
            const Reactor::Key& key,
            Fd fd,
            Polling::NotifyOn interest,
            Polling::Tag tag,
            Polling::Mode mode = Polling::Mode::Level) = 0;

    virtual void registerFdOneShot(
            const Reactor::Key& key,
            Fd fd,
            Polling::NotifyOn interest,
            Polling::Tag tag,
            Polling::Mode mode = Polling::Mode::Level) = 0;

    virtual void modifyFd(
            const Reactor::Key& key,
            Fd fd,
            Polling::NotifyOn interest,
            Polling::Tag tag,
            Polling::Mode mode = Polling::Mode::Level) = 0;

    virtual void runOnce() = 0;
    virtual void run() = 0;

    virtual void shutdown() = 0;

    Reactor* reactor_;
};

struct SyncImpl : public Reactor::Impl {

    SyncImpl(Reactor* reactor)
        : Reactor::Impl(reactor)
    { }

    Reactor::Key addHandler(
            const std::shared_ptr<Handler>& handler) {

        handler->registerPoller(poller);

        handler->reactor_ = reactor_;

        auto key = handlers.add(handler);
        handler->key_ = key;

        return key;
    }

    std::shared_ptr<Handler> handler(
            const Reactor::Key& key,
            Fd fd) const {
        return handlers[key.index()];
    }

    void registerFd(
            const Reactor::Key& key,
            Fd fd,
            Polling::NotifyOn interest,
            Polling::Tag tag,
            Polling::Mode mode = Polling::Mode::Level) {

        auto pollKey = pollTag(key, tag);
        poller.addFd(fd, interest, pollKey, mode);
    }

    void registerFdOneShot(
            const Reactor::Key& key,
            Fd fd,
            Polling::NotifyOn interest,
            Polling::Tag tag,
            Polling::Mode mode = Polling::Mode::Level) {
        auto pollKey = pollTag(key, tag);
        poller.addFdOneShot(fd, interest, pollKey, mode);
    }

    void modifyFd(
            const Reactor::Key& key,
            Fd fd,
            Polling::NotifyOn interest,
            Polling::Tag tag,
            Polling::Mode mode = Polling::Mode::Level) {
        auto pollKey = pollTag(key, tag);
        poller.rearmFd(fd, interest, tag, mode);
    }

    void runOnce() {
        if (handlers.empty())
            throw std::runtime_error("You need to set at least one handler");

        std::chrono::milliseconds timeout(-1);

        for (;;) {
            std::vector<Polling::Event> events;
            int ready_fds;
            switch (ready_fds = poller.poll(events, 1024, timeout)) {
                case -1: break;
                case 0: break;
                default:
                    if (shutdown_) return;

                    handleFds(std::move(events));

                    timeout = std::chrono::milliseconds(0);
            }
        }
    }

    void run() {
        handlers.forEachHandler([](const std::shared_ptr<Handler> handler) {
            handler->context_.tid = std::this_thread::get_id();
        });

        while (!shutdown_)
            runOnce();
    }

    void shutdown() {
        shutdown_.store(true);
        shutdownFd.notify();
    }

private:

    Polling::Tag pollTag(const Reactor::Key& key, Polling::Tag tag) const {
        uint64_t value = tag.value();
        auto encodedValue = HandlerList::encodeValue(key, value);

    }
    std::pair<size_t, uint64_t> decodeTag(const Polling::Tag& tag) const {
        return HandlerList::decodeValue(tag);
    }

    void handleFds(std::vector<Polling::Event> events) const {
        // Fast-path: if we only have one handler, do not bother scanning the fds to find
        // the right handlers
        if (handlers.size() == 1)
            handlers[0]->onReady(FdSet(std::move(events)));
        else {
            std::unordered_map<std::shared_ptr<Handler>, std::vector<Polling::Event>> fdHandlers;

            for (auto& event: events) {
                size_t index;
                uint64_t value;

                std::tie(index, value) = decodeTag(event.tag);
                auto handler = handlers[index];
                auto& evs = fdHandlers[handler];
                evs.push_back(std::move(event));
            }

            for (auto& data: fdHandlers) {
                data.first->onReady(FdSet(std::move(data.second)));
            }
        }
    }

    struct HandlerList {

        HandlerList() {
            std::fill(std::begin(handlers), std::end(handlers), nullptr);
        }

        HandlerList(const HandlerList& other) = delete;
        HandlerList& operator=(const HandlerList& other) = delete;

        HandlerList(HandlerList&& other) = default;
        HandlerList& operator=(HandlerList&& other) = default;

        HandlerList clone() const {
            HandlerList list;

            for (size_t i = 0; i < index_; ++i) {
                list.handlers[i] = handlers[i]->clone();
            }
            list.index_ = index_;

            return list;
        }

        Reactor::Key add(const std::shared_ptr<Handler>& handler) {
            if (index_ == MaxHandlers)
                throw std::runtime_error("Maximum handlers reached");

            Reactor::Key key(index_, handler);
            handlers[index_++] = handler;

            return key;
        }

        std::shared_ptr<Handler> operator[](size_t index) const {
            return handlers[index];
        }

        std::shared_ptr<Handler> at(size_t index) const {
            if (index >= index_)
                throw std::runtime_error("Attempting to retrieve invalid handler");

            return handlers[index];
        }

        bool empty() const {
            return index_ == 0;
        }

        size_t size() const {
            return index_;
        }

        static Polling::Tag encodeValue(const Reactor::Key& key, uint64_t value) {
            auto index = key.index();
            // The reason why we are using the most significant bits to encode
            // the index of the handler is that in the fast path, we won't need
            // to shift the value to retrieve the fd if there is only one handler as
            // all the bits will already be set to 0.
            auto encodedValue = (index << HandlerShift) | value;
            return Polling::Tag(value);
        }

        static std::pair<size_t, uint64_t> decodeValue(const Polling::Tag& tag) {
            auto value = tag.value();
            size_t index = value >> HandlerShift;
            uint64_t fd = value & DataMask;

            return std::make_pair(index, fd);
        }

        template<typename Func>
        void forEachHandler(Func func) const {
            for (size_t i = 0; i < index_; ++i)
                func(handlers[i]);
        }

    private:

        // We are using the highest 8 bits of the fd to encode the index of the handler,
        // which gives us a maximum of 2**8 - 1 handler, 255
        static constexpr size_t HandlerBits = 8;
        static constexpr size_t HandlerShift = sizeof(uint64_t) - HandlerBits;
        static constexpr uint64_t DataMask = uint64_t(-1) >> HandlerBits;

        static constexpr size_t MaxHandlers = (1 << HandlerBits) - 1;

        std::array<std::shared_ptr<Handler>, MaxHandlers> handlers;
        size_t index_;
    };

    HandlerList handlers;

    std::atomic<bool> shutdown_;
    NotifyFd shutdownFd;

    Polling::Epoll poller;
};

struct AsyncImpl : public Reactor::Impl {
    AsyncImpl(Reactor* reactor, size_t threads)
        : Reactor::Impl(reactor) {
        for (size_t i = 0; i < threads; ++i) {
            std::unique_ptr<Worker> wrk(new Worker(reactor));
            workers_.push_back(std::move(wrk));
        }
    }

    Reactor::Key addHandler(
            const std::shared_ptr<Handler>& handler) {

        // @Invariant: the key should always be the same for every worker
        Reactor::Key key;

        for (auto& wrk: workers_) {
            key = wrk->sync->addHandler(handler->clone());
        }

        return key;
    }

    std::shared_ptr<Handler> handler(
            const Reactor::Key& key, Fd fd) const {
        auto& worker = workers_[fd & workers_.size()];
        return worker->sync->handler(key, fd);
    }

    void registerFd(
            const Reactor::Key& key,
            Fd fd,
            Polling::NotifyOn interest,
            Polling::Tag tag,
            Polling::Mode mode = Polling::Mode::Level) {
        auto& worker = workers_[fd & workers_.size()];

        worker->sync->registerFd(
                key, fd, interest, tag, mode);
    }

    void registerFdOneShot(
            const Reactor::Key& key,
            Fd fd,
            Polling::NotifyOn interest,
            Polling::Tag tag,
            Polling::Mode mode = Polling::Mode::Level) {
        auto& worker = workers_[fd & workers_.size()];

        worker->sync->registerFdOneShot(
                key, fd, interest, tag, mode);
    }

    void modifyFd(
            const Reactor::Key& key,
            Fd fd,
            Polling::NotifyOn interest,
            Polling::Tag tag,
            Polling::Mode mode = Polling::Mode::Level) {
        auto& worker = workers_[fd & workers_.size()];

        worker->sync->modifyFd(key, fd, interest, tag, mode);
    }

    void runOnce() {
    }

    void run() {
        for (auto& wrk: workers_)
            wrk->run();
    }

    void shutdown() {
        for (auto& wrk: workers_)
            wrk->shutdown();
    }

private:
    struct Worker {

        Worker(Reactor* reactor) {
            sync.reset(new SyncImpl(reactor));
        }

        ~Worker() {
            if (thread)
                thread->join();
        }

        void run() {
            thread.reset(new std::thread([=]() {
                sync->run();
            }));
        }

        void shutdown() {
            sync->shutdown();
        }

        std::unique_ptr<std::thread> thread;
        std::unique_ptr<SyncImpl> sync;
    };

    std::vector<std::unique_ptr<Worker>> workers_;
};

Reactor::Key::Key()
    : index_(0)
    , handler_(nullptr)
{ }

Reactor::Key::Key(
        size_t index, const std::shared_ptr<Handler>& handler)
    : index_(index)
    , handler_(handler)
{ }

void
Reactor::Key::registerFd(
        Fd fd,
        Polling::NotifyOn interest, Polling::Tag tag,
        Polling::Mode mode)
{
    auto reactor = handler_->reactor();

    reactor->registerFd(*this, fd, interest, tag, mode);
}

void
Reactor::Key::registerFdOneShot(
        Fd fd,
        Polling::NotifyOn interest, Polling::Tag tag,
        Polling::Mode mode)
{
    auto reactor = handler_->reactor();
    reactor->registerFdOneShot(*this, fd, interest, tag, mode);
}

void
Reactor::Key::registerFd(
        Fd fd,
        Polling::NotifyOn interest,
        Polling::Mode mode)
{
    auto reactor = handler_->reactor();

    reactor->registerFd(*this, fd, interest, Polling::Tag(fd), mode);
}

void
Reactor::Key::registerFdOneShot(
        Fd fd,
        Polling::NotifyOn interest,
        Polling::Mode mode)
{
    auto reactor = handler_->reactor();

    reactor->registerFdOneShot(*this, fd, interest, Polling::Tag(fd), mode);
}

void
Reactor::Key::modifyFd(
        Fd fd,
        Polling::NotifyOn interest,
        Polling::Tag tag,
        Polling::Mode mode)
{
    auto reactor = handler_->reactor();

    reactor->modifyFd(*this, fd, interest, tag, mode);
}

void
Reactor::Key::modifyFd(
        Fd fd,
        Polling::NotifyOn interest,
        Polling::Mode mode)
{
    auto reactor = handler_->reactor();

    reactor->modifyFd(*this, fd, interest, Polling::Tag(fd), mode);
}

Reactor::Reactor() = default;

Reactor::~Reactor() = default;

std::shared_ptr<Reactor>
Reactor::create() {
    return std::make_shared<Reactor>();
}

void
Reactor::init() {
    SyncContext context;
    init(context);
}

void
Reactor::init(const ExecutionContext& context) {
    impl_.reset(context.makeImpl(this));
}

Reactor::Key
Reactor::addHandler(const std::shared_ptr<Handler>& handler) {
    return impl()->addHandler(handler);
}

std::shared_ptr<Handler>
Reactor::handler(const Reactor::Key& key, Fd fd) {
    return impl()->handler(key, fd);
}

void
Reactor::registerFd(
        const Reactor::Key& key, Fd fd, Polling::NotifyOn interest, Polling::Tag tag,
        Polling::Mode mode)
{
    impl()->registerFd(key, fd, interest, tag, mode);
}

void
Reactor::registerFdOneShot(
        const Reactor::Key& key, Fd fd, Polling::NotifyOn interest, Polling::Tag tag,
        Polling::Mode mode)
{
    impl()->registerFdOneShot(key, fd, interest, tag, mode);
}

void
Reactor::registerFd(
        const Reactor::Key& key, Fd fd, Polling::NotifyOn interest,
        Polling::Mode mode)
{
    impl()->registerFd(key, fd, interest, Polling::Tag(fd), mode);
}

void
Reactor::registerFdOneShot(
        const Reactor::Key& key, Fd fd, Polling::NotifyOn interest,
        Polling::Mode mode)
{
    impl()->registerFdOneShot(key, fd, interest, Polling::Tag(fd), mode);
}

void
Reactor::modifyFd(
        const Reactor::Key& key, Fd fd, Polling::NotifyOn interest, Polling::Tag tag,
        Polling::Mode mode)
{
    impl()->modifyFd(key, fd, interest, tag, mode);
}

void
Reactor::modifyFd(
        const Reactor::Key& key, Fd fd, Polling::NotifyOn interest,
        Polling::Mode mode)
{
    impl()->modifyFd(key, fd, interest, Polling::Tag(fd), mode);
}

void
Reactor::run() {
    impl()->run();
}

void
Reactor::shutdown() {
    impl()->shutdown();
}

void
Reactor::runOnce() {
    impl()->runOnce();
}

Reactor::Impl *
Reactor::impl() const {
    if (!impl_)
        throw std::runtime_error("Invalid object state, you should call init() before");

    return impl_.get();
}

Reactor::Impl*
SyncContext::makeImpl(Reactor* reactor) const {
    return new SyncImpl(reactor);
}

Reactor::Impl*
AsyncContext::makeImpl(Reactor* reactor) const {
    return new AsyncImpl(reactor, threads_);
}

AsyncContext
AsyncContext::singleThreaded() {
    return AsyncContext(1);
}

} // namespace Aio
