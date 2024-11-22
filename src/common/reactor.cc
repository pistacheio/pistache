/*
 * SPDX-FileCopyrightText: 2016 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
   Mathieu Stefani, 15 juin 2016

   Implementation of the Reactor
*/

#include <pistache/reactor.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <pistache/pist_quote.h>
#include <pistache/pist_timelog.h>

#ifdef _IS_BSD
// For pthread_set_name_np
#include PIST_QUOTE(PST_THREAD_HDR)
#ifndef __NetBSD__
#include <pthread_np.h>
#endif
#endif

#ifdef _IS_WINDOWS
#include <windows.h> // Needed for PST_THREAD_HDR (processthreadsapi.h)
#include PIST_QUOTE(PST_THREAD_HDR) // for SetThreadDescription
#endif

#ifdef _IS_WINDOWS
static std::atomic_bool lLoggedSetThreadDescriptionFail = false;
#ifdef __MINGW32__

#include <windows.h>
#include <libloaderapi.h> // for GetProcAddress and GetModuleHandleA
typedef HRESULT (WINAPI *TSetThreadDescription)(HANDLE, PCWSTR);

static std::atomic_bool lSetThreadDescriptionLoaded = false;
static std::mutex lSetThreadDescriptionLoadMutex;
static TSetThreadDescription lSetThreadDescriptionPtr = nullptr;

TSetThreadDescription getSetThreadDescriptionPtr()
{
    if (lSetThreadDescriptionLoaded)
        return(lSetThreadDescriptionPtr);

    GUARD_AND_DBG_LOG(lSetThreadDescriptionLoadMutex);
    if (lSetThreadDescriptionLoaded)
        return(lSetThreadDescriptionPtr);

    HMODULE hKernelBase = GetModuleHandleA("KernelBase.dll");

    if (!hKernelBase)
    {
        PS_LOG_WARNING(
            "Failed to get KernelBase.dll for SetThreadDescription");
        lSetThreadDescriptionLoaded = true;
        return(nullptr);
    }

    FARPROC set_thread_desc_fpptr =
        GetProcAddress(hKernelBase, "SetThreadDescription");

    // We do the cast in two steps, otherwise mingw-gcc complains about
    // incompatible types
    void * set_thread_desc_vptr =
        reinterpret_cast<void *>(set_thread_desc_fpptr);
    lSetThreadDescriptionPtr =
        reinterpret_cast<TSetThreadDescription>(set_thread_desc_vptr);

    lSetThreadDescriptionLoaded = true;
    if (!lSetThreadDescriptionPtr)
    {
        PS_LOG_WARNING(
            "Failed to get SetThreadDescription from KernelBase.dll");
    }
    return(lSetThreadDescriptionPtr);
}
#endif // of ifdef __MINGW32__
#endif // of ifdef _IS_WINDOWS

using namespace std::string_literals;

namespace Pistache::Aio
{

    class Reactor::Impl
    {
    public:
        Impl(Reactor* reactor)
            : reactor_(reactor)
        { }

        virtual ~Impl() = default;

        virtual Reactor::Key addHandler(
            const std::shared_ptr<Handler>& handler, bool setKey)
            = 0;

        virtual void detachFromReactor(
            const std::shared_ptr<Handler>& handler)
            = 0;

        virtual void detachAndRemoveAllHandlers() = 0;

        virtual std::vector<std::shared_ptr<Handler>>
        handlers(const Reactor::Key& key) const = 0;

        virtual void registerFd(const Reactor::Key& key, Fd fd,
                                Polling::NotifyOn interest, Polling::Tag tag,
                                Polling::Mode mode = Polling::Mode::Level)
            = 0;

        virtual void registerFdOneShot(const Reactor::Key& key, Fd fd,
                                       Polling::NotifyOn interest, Polling::Tag tag,
                                       Polling::Mode mode = Polling::Mode::Level)
            = 0;

        virtual void modifyFd(const Reactor::Key& key, Fd fd,
                              Polling::NotifyOn interest, Polling::Tag tag,
                              Polling::Mode mode = Polling::Mode::Level)
            = 0;

        virtual void removeFd(const Reactor::Key& key, Fd fd) = 0;

        virtual void runOnce() = 0;
        virtual void run()     = 0;

        virtual void shutdown() = 0;

        Reactor* reactor_;
    };

    /* Synchronous implementation of the reactor that polls in the context
     * of the same thread
     */
    class SyncImpl : public Reactor::Impl
    {
    public:
        explicit SyncImpl(Reactor* reactor)
            : Reactor::Impl(reactor)
            , handlers_()
            , shutdown_()
            , shutdownFd()
            , poller()
        {
            shutdownFd.bind(poller);
        }

        Reactor::Key addHandler(const std::shared_ptr<Handler>& handler,
                                bool setKey = true) override
        {
            handler->registerPoller(poller);

            handler->reactor_ = reactor_;

            std::mutex& poller_reg_unreg_mutex(poller.reg_unreg_mutex_);
            GUARD_AND_DBG_LOG(poller_reg_unreg_mutex);
            auto key = handlers_.add(handler);
            if (setKey)
                handler->key_ = key;

            return key;
        }

        // poller.reg_unreg_mutex_ must be locked before calling
        void detachFromReactor(const std::shared_ptr<Handler>& handler)
            override
        {
            PS_TIMEDBG_START_THIS;

            handler->unregisterPoller(poller);
            handler->reactor_ = nullptr;
        }

        void detachAndRemoveAllHandlers() override
        {
            std::mutex& poller_reg_unreg_mutex(poller.reg_unreg_mutex_);
            GUARD_AND_DBG_LOG(poller_reg_unreg_mutex);

            handlers_.forEachHandler([this](
                                         const std::shared_ptr<Handler> handler) {
                detachFromReactor(handler);
            });

            handlers_.removeAll();
        }

        std::shared_ptr<Handler> handler(const Reactor::Key& key) const
        {
            return handlers_.at(static_cast<size_t>(key.data()));
        }

        std::vector<std::shared_ptr<Handler>>
        handlers(const Reactor::Key& key) const override
        {
            std::vector<std::shared_ptr<Handler>> res;

            res.push_back(handler(key));
            return res;
        }

#ifdef DEBUG
        static void logNotifyOn(Fd fd, Polling::NotifyOn interest)
        {
            std::string str("Fd ");

            std::stringstream ss;
            ss << fd;
            str += ss.str();

            if ((static_cast<unsigned int>(interest)) & (static_cast<unsigned int>(Polling::NotifyOn::Read)))
                str += " read";
            if ((static_cast<unsigned int>(interest)) & (static_cast<unsigned int>(Polling::NotifyOn::Write)))
                str += " write";
            if ((static_cast<unsigned int>(interest)) & (static_cast<unsigned int>(Polling::NotifyOn::Hangup)))
                str += " hangup";
            if ((static_cast<unsigned int>(interest)) & (static_cast<unsigned int>(Polling::NotifyOn::Shutdown)))
                str += " shutdown";

            PS_LOG_DEBUG_ARGS("%s", str.c_str());
        }

#define PS_LOG_DBG_NOTIFY_ON logNotifyOn(fd, interest)
#else
#define PS_LOG_DBG_NOTIFY_ON
#endif

        void registerFd(const Reactor::Key& key, Fd fd, Polling::NotifyOn interest,
                        Polling::Tag tag,
                        Polling::Mode mode = Polling::Mode::Level) override
        {

            auto pollTag = encodeTag(key, tag);
            PS_LOG_DBG_NOTIFY_ON;
            poller.addFd(fd, Flags<Polling::NotifyOn>(interest), pollTag, mode);
        }

        void registerFdOneShot(const Reactor::Key& key, Fd fd,
                               Polling::NotifyOn interest, Polling::Tag tag,
                               Polling::Mode mode = Polling::Mode::Level) override
        {
            PS_TIMEDBG_START_ARGS("Fd %" PIST_QUOTE(PS_FD_PRNTFCD), fd);

            auto pollTag = encodeTag(key, tag);
            PS_LOG_DBG_NOTIFY_ON;
            poller.addFdOneShot(fd, Flags<Polling::NotifyOn>(interest), pollTag, mode);
        }

        void modifyFd(const Reactor::Key& key, Fd fd, Polling::NotifyOn interest,
                      Polling::Tag tag,
                      Polling::Mode mode = Polling::Mode::Level) override
        {

            auto pollTag = encodeTag(key, tag);
            poller.rearmFd(fd, Flags<Polling::NotifyOn>(interest), pollTag, mode);
        }

        void removeFd(const Reactor::Key& /*key*/, Fd fd) override
        {
            PS_TIMEDBG_START_ARGS("Reactor %p, Fd %" PIST_QUOTE(PS_FD_PRNTFCD),
                                  this, fd);

            poller.removeFd(fd);
        }

        void runOnce() override
        {
            PS_TIMEDBG_START;

            if (handlers_.empty())
                throw std::runtime_error("You need to set at least one handler");

            for (;;)
            {
                PS_TIMEDBG_START;
                { // encapsulate l_guard(poller.reg_unreg_mutex_)
                  // See comment in class Epoll regarding reg_unreg_mutex_

                    std::mutex&
                        poller_reg_unreg_mutex(poller.reg_unreg_mutex_);
                    GUARD_AND_DBG_LOG(poller_reg_unreg_mutex);

                    std::vector<Polling::Event> events;
                    int ready_fds = poller.poll(events);

                    switch (ready_fds)
                    {
                    case -1:
                        break;
                    case 0:
                        break;
                    default:
                        {
                            if (shutdown_)
                                return;

                            GUARD_AND_DBG_LOG(shutdown_mutex_);
                            if (shutdown_)
                                return;

                            handleFds(std::move(events));
                        }
                    }
                }
            }
        }

        void run() override
        {
            PS_TIMEDBG_START;

            // Note: poller_reg_unreg_mutex is already locked (by
            // Listener::run()) before calling here, so it is safe to call
            // handlers_.forEachHandler here

            handlers_.forEachHandler([](const std::shared_ptr<Handler> handler) {
                handler->context_.tid = std::this_thread::get_id();
            });

            while (!shutdown_)
            {
                PS_TIMEDBG_START;
                runOnce();
            }
        }

        void shutdown() override
        {
            PS_TIMEDBG_START_THIS;

            shutdown_.store(true);

            GUARD_AND_DBG_LOG(shutdown_mutex_);
            shutdownFd.notify();
        }

        static constexpr size_t MaxHandlers() { return HandlerList::MaxHandlers; }

    private:
        static Polling::Tag encodeTag(const Reactor::Key& key, Polling::Tag tag)
        {
            auto value = tag.value();
            return HandlerList::encodeTag(key, value);
        }

        static std::pair<size_t, Polling::TagValue>
        decodeTag(const Polling::Tag& tag)
        {
            return HandlerList::decodeTag(tag);
        }

        void handleFds(std::vector<Polling::Event> events) const
        {
            // Fast-path: if we only have one handler, do not bother scanning the fds to
            // find the right handlers
            if (handlers_.size() == 1)
                handlers_.at(0)->onReady(FdSet(std::move(events)));
            else
            {
                std::unordered_map<std::shared_ptr<Handler>, std::vector<Polling::Event>>
                    fdHandlers;

                for (auto& event : events)
                {
                    size_t index;
                    Polling::TagValue value;

                    std::tie(index, value) = decodeTag(event.tag);
                    auto handler_          = handlers_.at(index);
                    auto& evs              = fdHandlers.at(handler_);
                    evs.push_back(std::move(event));
                }

                for (auto& data : fdHandlers)
                {
                    data.first->onReady(FdSet(std::move(data.second)));
                }
            }
        }

        struct HandlerList
        {

            // We are using the highest 8 bits of the fd to encode the index of the
            // handler, which gives us a maximum of 2**8 - 1 handler, 255
            static constexpr size_t HandlerBits  = 8;
            static constexpr size_t HandlerShift = sizeof(uint64_t) - HandlerBits;
            static constexpr uint64_t DataMask   = uint64_t(-1) >> HandlerBits;

            static constexpr size_t MaxHandlers = (1 << HandlerBits) - 1;

            HandlerList()
                : handlers()
                , index_()
            {
                std::fill(std::begin(handlers), std::end(handlers), nullptr);
            }

            HandlerList(const HandlerList& other)            = delete;
            HandlerList& operator=(const HandlerList& other) = delete;

            // poller.reg_unreg_mutex_ must be locked before calling
            Reactor::Key add(const std::shared_ptr<Handler>& handler)
            {
                if (index_ == MaxHandlers)
                    throw std::runtime_error("Maximum handlers reached");

                Reactor::Key key(index_);
                handlers.at(index_++) = handler;

                return key;
            }

            // poller.reg_unreg_mutex_ must be locked before calling
            void removeAll()
            {
                index_ = 0;
                handlers.fill(nullptr);
            }

            // poller.reg_unreg_mutex_ must be locked before calling
            std::shared_ptr<Handler> at(size_t index) const
            {
                if (index >= index_)
                    throw std::runtime_error("Attempting to retrieve invalid handler");
                return handlers.at(index);
            }

            bool empty() const { return index_ == 0; }

            size_t size() const { return index_; }

            // Note that in the _USE_LIBEVENT case the the tag has type "struct
            // event *" but in fact may be that pointer with high bits set to
            // the value of "index". So in the _USE_LIBEVENT case we must be
            // careful to mask out those high bits to retrieve the actual
            // pointer, just as, in the non-_USE_LIBEVENT case, we have to mask
            // those high bits to retrieve the actual file descriptor.
            static Polling::Tag encodeTag(const Reactor::Key& key,
                                          Polling::TagValueConst value)
            {
                auto index = key.data();
                // The reason why we are using the most significant bits to
                // encode the index of the handler is that in the fast path, we
                // won't need to shift the value to retrieve the fd if there is
                // only one handler as all the bits will already be set to 0.
                auto encodedValue                =
                    (index << HandlerShift) |
                    PS_FD_CAST_TO_UNUM(uint64_t, static_cast<Fd>(value));
                Polling::TagValue encodedValueTV =
                    static_cast<Polling::TagValue>(PS_NUM_CAST_TO_FD(encodedValue));
                return Polling::Tag(encodedValueTV);
            }

            static std::pair<size_t, Polling::TagValue>
            decodeTag(const Polling::Tag& tag)
            {
                auto value                      = tag.valueU64();
                size_t index                    = value >> HandlerShift;
                uint64_t maskedValue            = value & DataMask;
                Polling::TagValue maskedValueTV =
                    static_cast<Polling::TagValue>(PS_NUM_CAST_TO_FD(maskedValue));

                return std::make_pair(index, maskedValueTV);
            }

            // poller.reg_unreg_mutex_ must be locked before calling
            template <typename Func>
            void forEachHandler(Func func) const
            {
                for (size_t i = 0; i < index_; ++i)
                    func(handlers.at(i));
            }

        private:
            std::array<std::shared_ptr<Handler>, MaxHandlers> handlers;
            size_t index_;
        };

        HandlerList handlers_;

        std::mutex shutdown_mutex_;
        std::atomic<bool> shutdown_;
        NotifyFd shutdownFd;

        Polling::Epoll poller;
    };

    /* Asynchronous implementation of the reactor that spawns a number N of threads
     * and creates a polling fd per thread
     *
     * Implementation detail:
     *
     *  Here is how it works: the implementation simply starts a synchronous variant
     *  of the implementation in its own std::thread. When adding an handler, it
     * will add a clone() of the handler to every worker (thread), and assign its
     * own key to the handler. Here is where things start to get interesting. Here
     * is how the key encoding works for every handler:
     *
     *  [     handler idx      ] [       worker idx         ]
     *  ------------------------ ----------------------------
     *       ^ 32 bits                   ^ 32 bits
     *  -----------------------------------------------------
     *                       ^ 64 bits
     *
     * Since we have up to 64 bits of data for every key, we encode the index of the
     * handler that has been assigned by the SyncImpl in the upper 32 bits, and
     * encode the index of the worker thread in the lowest 32 bits.
     *
     * When registering a fd for a given key, the AsyncImpl then knows which worker
     * to use by looking at the lowest 32 bits of the Key's data. The SyncImpl will
     * then use the highest 32 bits to retrieve the index of the handler.
     */

    class AsyncImpl : public Reactor::Impl
    {
    public:
        static constexpr uint32_t KeyMarker = 0xBADB0B;

        AsyncImpl(Reactor* reactor,
                  size_t threads, const std::string& threadsName)
            : Reactor::Impl(reactor)
        {
            PS_TIMEDBG_START_THIS;

            if (threads > SyncImpl::MaxHandlers())
                throw std::runtime_error("Too many worker threads requested (max "s + std::to_string(SyncImpl::MaxHandlers()) + ")."s);

            for (size_t i = 0; i < threads; ++i)
                workers_.emplace_back(std::make_unique<Worker>(reactor, threadsName));
            PS_LOG_DEBUG_ARGS("threads %d, workers_.size() %d",
                              threads, workers_.size());
        }

        Reactor::Key addHandler(const std::shared_ptr<Handler>& handler,
                                bool) override
        {
            PS_TIMEDBG_START_THIS;

            std::array<Reactor::Key, SyncImpl::MaxHandlers()> keys;

            for (size_t i = 0; i < workers_.size(); ++i)
            {
                auto& wrk = workers_.at(i);

                auto cl     = handler->clone();
                auto key    = wrk->sync->addHandler(cl, false /* setKey */);
                auto newKey = encodeKey(key, static_cast<uint32_t>(i));
                cl->key_    = newKey;

                keys.at(i) = key;
            }

            auto data = keys.at(0).data() << 32 | KeyMarker;

            return Reactor::Key(data);
        }

        void detachFromReactor(const std::shared_ptr<Handler>& handler)
            override
        {
            for (size_t i = 0; i < workers_.size(); ++i)
            {
                auto& wrk = workers_.at(i);

                wrk->sync->detachFromReactor(handler);
            }
        }

        void detachAndRemoveAllHandlers() override
        {
            for (size_t i = 0; i < workers_.size(); ++i)
            {
                auto& wrk = workers_.at(i);

                wrk->sync->detachAndRemoveAllHandlers();
            }
        }

        std::vector<std::shared_ptr<Handler>>
        handlers(const Reactor::Key& key) const override
        {

            const std::pair<uint32_t, uint32_t> idx_marker = decodeKey(key);
            if (idx_marker.second != KeyMarker)
                throw std::runtime_error("Invalid key");

            Reactor::Key originalKey(idx_marker.first);

            std::vector<std::shared_ptr<Handler>> res;
            res.reserve(workers_.size());
            for (const auto& wrk : workers_)
            {
                res.push_back(wrk->sync->handler(originalKey));
            }

            return res;
        }

        void registerFd(const Reactor::Key& key, Fd fd, Polling::NotifyOn interest,
                        Polling::Tag tag,
                        Polling::Mode mode = Polling::Mode::Level) override
        {
            PS_TIMEDBG_START_THIS;

            dispatchCall(key, &SyncImpl::registerFd, fd, interest, tag, mode);
        }

        void registerFdOneShot(const Reactor::Key& key, Fd fd,
                               Polling::NotifyOn interest, Polling::Tag tag,
                               Polling::Mode mode = Polling::Mode::Level) override
        {
            PS_TIMEDBG_START_THIS;

            dispatchCall(key, &SyncImpl::registerFdOneShot, fd, interest, tag, mode);
        }

        void modifyFd(const Reactor::Key& key, Fd fd, Polling::NotifyOn interest,
                      Polling::Tag tag,
                      Polling::Mode mode = Polling::Mode::Level) override
        {
            PS_TIMEDBG_START_THIS;

            dispatchCall(key, &SyncImpl::modifyFd, fd, interest, tag, mode);
        }

        void removeFd(const Reactor::Key& key, Fd fd) override
        {
            PS_TIMEDBG_START_ARGS("this %p, Fd %" PIST_QUOTE(PS_FD_PRNTFCD),
                                  this, fd);
            dispatchCall(key, &SyncImpl::removeFd, fd);
        }

        void runOnce() override { }

        void run() override
        {
            for (auto& wrk : workers_)
                wrk->run();
        }

        void shutdown() override
        {
            for (auto& wrk : workers_)
                wrk->shutdown();
        }

    private:
        static Reactor::Key encodeKey(const Reactor::Key& originalKey,
                                      uint32_t value)
        {
            auto data     = originalKey.data();
            auto newValue = data << 32 | value;
            return Reactor::Key(newValue);
        }

        static std::pair<uint32_t, uint32_t>
        decodeKey(const Reactor::Key& encodedKey)
        {
            auto data = encodedKey.data();
            auto hi   = static_cast<uint32_t>(data >> 32);
            auto lo   = static_cast<uint32_t>(data & 0xFFFFFFFF);
            return std::make_pair(hi, lo);
        }

#define CALL_MEMBER_FN(obj, pmf) (obj->*(pmf))

        template <typename Func, typename... Args>
        void dispatchCall(const Reactor::Key& key, Func func, Args&&... args) const
        {
            PS_TIMEDBG_START_THIS;
            PS_LOG_DEBUG_ARGS("workers_.size() %d", workers_.size());

            auto decoded    = decodeKey(key);
            const auto& wrk = workers_.at(decoded.second);

            Reactor::Key originalKey(decoded.first);

            CALL_MEMBER_FN(wrk->sync.get(), func)
            (originalKey, std::forward<Args>(args)...);
        }

#undef CALL_MEMBER_FN

        struct Worker
        {

            explicit Worker(Reactor* reactor, const std::string& threadsName)
                : thread()
                , sync(new SyncImpl(reactor))
                , threadsName_(threadsName)
            { }

            ~Worker()
            {
                if (thread.joinable())
                    thread.join();
            }

            void run()
            {
                PS_TIMEDBG_START;

                thread = std::thread([=]() {
                    PS_TIMEDBG_START;

                    if (!threadsName_.empty())
                    {
                        PS_LOG_DEBUG("Setting thread name/description");
#ifdef _IS_WINDOWS
                        const std::string threads_name(threadsName_.substr(0, 15));
                        const std::wstring temp(threads_name.begin(),
                                          threads_name.end());
                        const LPCWSTR wide_threads_name = temp.c_str();

                        HRESULT hr = E_NOTIMPL;
#ifdef __MINGW32__
                        TSetThreadDescription set_thread_description_ptr =
                            getSetThreadDescriptionPtr();
                        if (set_thread_description_ptr)
                        {
                            hr = set_thread_description_ptr(
                                GetCurrentThread(), wide_threads_name);
                        }
#else
                        hr = SetThreadDescription(GetCurrentThread(),
                                                  wide_threads_name);
#endif
                        if ((FAILED(hr)) && (!lLoggedSetThreadDescriptionFail))
                        {
                            lLoggedSetThreadDescriptionFail = true;
                            // Log it just once
                            PS_LOG_INFO("SetThreadDescription failed");
                        }
#else
#if defined _IS_BSD && !defined __NetBSD__
                        pthread_set_name_np(
#else
                        pthread_setname_np(
#endif
#ifndef __APPLE__
                            // Apple's macOS version of pthread_setname_np
                            // takes only "const char * name" as parm
                            // (Nov/2023), and assumes that the thread is the
                            // calling thread. Note that pthread_self returns
                            // calling thread in Linux, so this amounts to
                            // the same thing in the end
                            // It appears older FreeBSD (2003 ?) also behaves
                            // as per macOS, while newer FreeBSD (2021 ?)
                            // behaves as per Linux
                            pthread_self(),
#endif
#ifdef __NetBSD__
                            "%s", // NetBSD has 3 parms for pthread_setname_np
                            (void*)/*cast away const for NetBSD*/
#endif
                            threadsName_.substr(0, 15)
                                .c_str());
#endif // of ifdef _IS_WINDOWS... else...
                    }
                    PS_LOG_DEBUG("Calling sync->run()");
                    sync->run();
                });
            }

            void shutdown() { sync->shutdown(); }

            std::thread thread;
            std::unique_ptr<SyncImpl> sync;
            std::string threadsName_;
        };

        std::vector<std::unique_ptr<Worker>> workers_;
    };

    Reactor::Key::Key()
        : data_(0)
    { }

    Reactor::Key::Key(uint64_t data)
        : data_(data)
    { }

    Reactor::Reactor() = default;

    // Reactor::~Reactor() = default;
    Reactor::~Reactor()
    {
        PS_TIMEDBG_START_THIS;

        detachAndRemoveAllHandlers();
    }

    std::shared_ptr<Reactor> Reactor::create()
    {
        PS_TIMEDBG_START;
        return std::make_shared<Reactor>();
    }

    void Reactor::init()
    {
        SyncContext context;
        init(context);
    }

    void Reactor::init(const ExecutionContext& context)
    {
        PS_TIMEDBG_START_THIS;

        Reactor::Impl* new_impl = context.makeImpl(this);
        impl_.reset(new_impl);
    }

    Reactor::Key Reactor::addHandler(const std::shared_ptr<Handler>& handler)
    {
        PS_TIMEDBG_START_THIS;
        return impl()->addHandler(handler, true);
    }

    void Reactor::detachFromReactor(const std::shared_ptr<Handler>& handler)
    {
        PS_TIMEDBG_START_THIS;
        return impl()->detachFromReactor(handler);
    }

    void Reactor::detachAndRemoveAllHandlers()
    {
        PS_TIMEDBG_START_THIS;

        if (impl_) // may be null if Reactor::~Reactor called before we've had
                   // a chance to call Reactor::init()
            impl()->detachAndRemoveAllHandlers();
    }

    std::vector<std::shared_ptr<Handler>>
    Reactor::handlers(const Reactor::Key& key)
    {
        PS_TIMEDBG_START_THIS;
        return impl()->handlers(key);
    }

    void Reactor::registerFd(const Reactor::Key& key, Fd fd,
                             Polling::NotifyOn interest, Polling::Tag tag,
                             Polling::Mode mode)
    {
        PS_TIMEDBG_START_THIS;
        impl()->registerFd(key, fd, interest, tag, mode);
    }

    void Reactor::registerFdOneShot(const Reactor::Key& key, Fd fd,
                                    Polling::NotifyOn interest, Polling::Tag tag,
                                    Polling::Mode mode)
    {
        PS_TIMEDBG_START_THIS;
        impl()->registerFdOneShot(key, fd, interest, tag, mode);
    }

    void Reactor::registerFd(const Reactor::Key& key, Fd fd,
                             Polling::NotifyOn interest, Polling::Mode mode)
    {
        PS_TIMEDBG_START_THIS;
        impl()->registerFd(key, fd, interest, Polling::Tag(fd), mode);
    }

    void Reactor::registerFdOneShot(const Reactor::Key& key, Fd fd,
                                    Polling::NotifyOn interest,
                                    Polling::Mode mode)
    {
        PS_TIMEDBG_START_THIS;
        impl()->registerFdOneShot(key, fd, interest,
                                  Polling::Tag(fd), mode);
    }

    void Reactor::modifyFd(const Reactor::Key& key, Fd fd,
                           Polling::NotifyOn interest, Polling::Tag tag,
                           Polling::Mode mode)
    {
        PS_TIMEDBG_START_THIS;
        impl()->modifyFd(key, fd, interest, tag, mode);
    }

    void Reactor::modifyFd(const Reactor::Key& key, Fd fd,
                           Polling::NotifyOn interest, Polling::Mode mode)
    {
        PS_TIMEDBG_START_THIS;
        impl()->modifyFd(key, fd, interest, Polling::Tag(fd), mode);
    }

    void Reactor::removeFd(const Reactor::Key& key, Fd fd)
    {
        PS_TIMEDBG_START_ARGS("Reactor %p, Fd %" PIST_QUOTE(PS_FD_PRNTFCD),
                              this, fd);

        impl()->removeFd(key, fd);
    }

    void Reactor::run() { impl()->run(); }

    void Reactor::shutdown()
    {
        PS_TIMEDBG_START_THIS;

        if (impl_)
            impl()->shutdown();
    }

    void Reactor::runOnce() { impl()->runOnce(); }

    Reactor::Impl* Reactor::impl() const
    {
        if (!impl_)
            throw std::runtime_error(
                "Invalid object state, you should call init() before.");

        return impl_.get();
    }

    Reactor::Impl* SyncContext::makeImpl(Reactor* reactor) const
    {
        PS_TIMEDBG_START_THIS;
        return new SyncImpl(reactor);
    }

    Reactor::Impl* AsyncContext::makeImpl(Reactor* reactor) const
    {
        PS_TIMEDBG_START_THIS;
        return new AsyncImpl(reactor, threads_, threadsName_);
    }

    AsyncContext AsyncContext::singleThreaded() { return AsyncContext(1); }

} // namespace Pistache::Aio
