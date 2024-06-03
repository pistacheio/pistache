/*
 * SPDX-FileCopyrightText: 2015 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* os.cc
   Mathieu Stefani, 13 August 2015

*/

#include <pistache/common.h>
#include <pistache/config.h>
#include <pistache/os.h>

#include <fcntl.h>

#include <pistache/pist_timelog.h>

#include <pistache/eventmeth.h>
#include <pistache/pist_quote.h>
#ifndef _USE_LIBEVENT
#include <sys/epoll.h>
#endif

#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <thread>

namespace Pistache
{
    uint hardware_concurrency() { return std::thread::hardware_concurrency(); }

    bool make_non_blocking(int fd)
    {
        PS_TIMEDBG_START;

        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1)
        {
            PS_LOG_WARNING_ARGS("make_non_blocking fail for fd %" PIST_QUOTE(PS_FD_PRNTFCD), fd);
            return false;
        }

        flags |= O_NONBLOCK;
        int ret = fcntl(fd, F_SETFL, flags);
#ifdef DEBUG
        if (ret == -1)
        {
            PS_LOG_WARNING_ARGS("make_non_blocking fail for fd %" PIST_QUOTE(PS_FD_PRNTFCD), fd);
        }
#endif

        return ret != -1;
    }

    CpuSet::CpuSet() { bits.reset(); }

    CpuSet::CpuSet(std::initializer_list<size_t> cpus) { set(cpus); }

    void CpuSet::clear() { bits.reset(); }

    CpuSet& CpuSet::set(size_t cpu)
    {
        if (cpu >= Size)
        {
            throw std::invalid_argument("Trying to set invalid cpu number");
        }

        bits.set(cpu);
        return *this;
    }

    CpuSet& CpuSet::unset(size_t cpu)
    {
        if (cpu >= Size)
        {
            throw std::invalid_argument("Trying to unset invalid cpu number");
        }

        bits.set(cpu, false);
        return *this;
    }

    CpuSet& CpuSet::set(std::initializer_list<size_t> cpus)
    {
        for (auto cpu : cpus)
            set(cpu);
        return *this;
    }

    CpuSet& CpuSet::unset(std::initializer_list<size_t> cpus)
    {
        for (auto cpu : cpus)
            unset(cpu);
        return *this;
    }

    CpuSet& CpuSet::setRange(size_t begin, size_t end)
    {
        if (begin > end)
        {
            throw std::range_error("Invalid range, begin > end");
        }

        for (size_t cpu = begin; cpu < end; ++cpu)
        {
            set(cpu);
        }

        return *this;
    }

    CpuSet& CpuSet::unsetRange(size_t begin, size_t end)
    {
        if (begin > end)
        {
            throw std::range_error("Invalid range, begin > end");
        }

        for (size_t cpu = begin; cpu < end; ++cpu)
        {
            unset(cpu);
        }

        return *this;
    }

    bool CpuSet::isSet(size_t cpu) const
    {
        if (cpu >= Size)
        {
            throw std::invalid_argument("Trying to test invalid cpu number");
        }

        return bits.test(cpu);
    }

    size_t CpuSet::count() const { return bits.count(); }

#ifdef _POSIX_C_SOURCE
    cpu_set_t CpuSet::toPosix() const
    {
        cpu_set_t cpu_set;
        CPU_ZERO(&cpu_set);

        for (size_t cpu = 0; cpu < Size; ++cpu)
        {
            if (bits.test(cpu))
                CPU_SET(cpu, &cpu_set);
        }

        return cpu_set;
    }
#endif

    namespace Polling
    {

        Event::Event(Tag _tag)
            : flags()
            , tag(_tag)
        { }

        Epoll::Epoll()
            : epoll_fd([&]()
#ifdef _USE_LIBEVENT
                       { return TRY_NULL_RET(EventMethFns::create(
                             Const::MaxEvents)); }
#else
                       { return TRY_RET(epoll_create(Const::MaxEvents)); }
#endif
                       ())
        { }

        Epoll::~Epoll()
        {
#ifdef _USE_LIBEVENT
            if (epoll_fd != nullptr)
                epoll_fd = 0; // EventMethEpollEquiv destructor to be called
#else
            if (epoll_fd >= 0)
                close(epoll_fd);
#endif
        }

        void Epoll::addFd(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode)
        {
            PS_TIMEDBG_START_ARGS("fd %" PIST_QUOTE(PS_FD_PRNTFCD), fd);

#ifdef _USE_LIBEVENT
            short events = (short)epoll_fd->toEvEvents(interest);
            events |= EVM_PERSIST; // since EPOLLONESHOT not to be set

            if (mode == Mode::Edge)
                events |= EVM_ET;
            EventMethFns::setEmEventUserData(fd, tag.value_);

            TRY(epoll_fd->ctl(EvCtlAction::Add,
                              fd, events, NULL /* time */));

#else
            struct epoll_event ev;
            ev.events = toEpollEvents(interest);
            if (mode == Mode::Edge)
                ev.events |= EPOLLET;
            ev.data.u64 = tag.value_;

            TRY(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev));
#endif
        }

        void Epoll::addFdOneShot(Fd fd, Flags<NotifyOn> interest,
                                 Tag tag, Mode mode)
        {
            PS_TIMEDBG_START_ARGS("fd %" PIST_QUOTE(PS_FD_PRNTFCD), fd);

#ifdef _USE_LIBEVENT
            short events = (short)epoll_fd->toEvEvents(interest);
            if (mode == Mode::Edge)
                events |= EVM_ET;

            // EPOLLONESHOT: after an event notified for the FD, the FD is
            // disabled in the interest list and no other events will be
            // reported.  The user must rearm the FD with a new event mask.

            // In libevent, there is the EV_PERSIST flag, which is the
            // equivalent of the inverse of EPOLLONESHOT. So for libevent any
            // event is assumed to be "oneshot" unless EVM_PERSIST is set.

            EventMethFns::setEmEventUserData(fd, tag.value_);
            TRY(epoll_fd->ctl(EvCtlAction::Add,
                              fd, events, NULL /* time */));
#else
            struct epoll_event ev;
            ev.events = toEpollEvents(interest);
            ev.events |= EPOLLONESHOT;
            if (mode == Mode::Edge)
                ev.events |= EPOLLET;
            ev.data.u64 = tag.value_;

            TRY(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev));
#endif
        }

        void Epoll::removeFd(Fd fd)
        {
            PS_TIMEDBG_START_ARGS("fd %" PIST_QUOTE(PS_FD_PRNTFCD), fd);

#ifdef _USE_LIBEVENT
            TRY(epoll_fd->ctl(EvCtlAction::Del,
                              fd, 0 /* events */, NULL /* time */));
#else
            struct epoll_event ev;
            TRY(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev));
#endif
        }

        void Epoll::rearmFd(Fd fd, Flags<NotifyOn> interest, Tag tag,
                            Mode mode)
        {
            PS_TIMEDBG_START_ARGS("fd %" PIST_QUOTE(PS_FD_PRNTFCD), fd);

#ifdef _USE_LIBEVENT
            short events = (short)epoll_fd->toEvEvents(interest);

            // Why do we set EVM_PERSIST here? Since rearmFd is being called,
            // presumably fd was previously a one-shot event. You might think
            // it should continue to be a one-shot event. However, that does
            // not seem to be correct.

            // Per epoll_ctl man page, for CTL_MOD epoll_ctl will "Change the
            // settings associated with fd in the interest list to the new
            // settings specified in event [event being a parm to
            // epoll_ctl]". So CTL_MOD epoll_ctl will not retain EPOLLONESHOT
            // as a flag to the fd even if EPOLLONESHOT was previously set for
            // the fd. Accordingly, we must pass EVM_PERSIST here to mimic the
            // behaviour correctly, since Pistache does not set EPOLLONESHOT in
            // the epoll_ctl call below.
            events |= EVM_PERSIST;

            if (mode == Mode::Edge)
                events |= EVM_ET;
            EventMethFns::setEmEventUserData(fd, tag.value_);
            TRY(epoll_fd->ctl(EvCtlAction::Mod,
                              fd, events, NULL /* time */));

#else
            struct epoll_event ev;
            ev.events = toEpollEvents(interest);
            if (mode == Mode::Edge)
                ev.events |= EPOLLET;
            ev.data.u64 = tag.value_;

            TRY(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev));
#endif
        }

#ifdef DEBUG
        static void logFdAndNotifyOn(int i,
#ifdef _USE_LIBEVENT
                                     const EventMethEpollEquiv*
#else
                                     Fd
#endif
                                         epoll_fd,
                                     Fd fd,
                                     Polling::NotifyOn interest)
        {
            std::string str("#");

            std::stringstream ss;
            ss << i;
            str += ss.str();

            str += " epoll_fd ";

            std::stringstream ss2;
            ss2 << epoll_fd;
            str += ss2.str();

            str += ", fd ";

            std::stringstream ss3;
            ss3 << fd;
            str += ss3.str();

            if (((unsigned int)interest) & ((unsigned int)Polling::NotifyOn::Read))
                str += " read";
            if (((unsigned int)interest) & ((unsigned int)Polling::NotifyOn::Write))
                str += " write";
            if (((unsigned int)interest) & ((unsigned int)Polling::NotifyOn::Hangup))
                str += " hangup";
            if (((unsigned int)interest) & ((unsigned int)Polling::NotifyOn::Shutdown))
                str += " shutdown";

            PS_LOG_DEBUG_ARGS("%s", str.c_str());
        }

#ifdef _USE_LIBEVENT
#define PS_LOG_DBG_FD_AND_NOTIFY logFdAndNotifyOn(i,                  \
                                                  epoll_fd.get(),     \
                                                  (Fd)tag.valueU64(), \
                                                  event.flags)
#else
#define PS_LOG_DBG_FD_AND_NOTIFY logFdAndNotifyOn(i,                  \
                                                  epoll_fd,           \
                                                  (Fd)tag.valueU64(), \
                                                  event.flags)
#endif
#else
#define PS_LOG_DBG_FD_AND_NOTIFY
#endif

        int Epoll::poll(std::vector<Event>& events,
                        const std::chrono::milliseconds timeout) const
        {
#ifdef _USE_LIBEVENT
            // Note; We can't use PIST_QUOTE(PS_FD_PRNTFCD) for this logging
            // because the Fd expression is different ("epoll_fd.get()"
            // vs. "epoll_fd")
            PS_TIMEDBG_START_ARGS("getReadyEmEvents on EMEE (epoll_fd) %p",
                                  epoll_fd.get());
#else
            PS_TIMEDBG_START_ARGS("epoll on EMEE (epoll_fd) %d",
                                  epoll_fd);
#endif
            
#ifdef _USE_LIBEVENT
            std::set<Fd> ready_evm_events;
            int ready_evs = -1;

            try { // wrapping a try/catch around this to make sure we don't
                  // miss out on calling unlockInterestMutexIfLocked below
            do
            {
                ready_evs = epoll_fd->getReadyEmEvents(
                          static_cast<int>(timeout.count()), ready_evm_events);
            } while (ready_evs < 0 && errno == EINTR);

            PS_LOG_DEBUG_ARGS("ready_evs %d", ready_evs);

            if ((ready_evs > 0) && (!ready_evm_events.empty()))
            {
#ifdef DEBUG
                int i = 0;
#endif
                for (std::set<Fd>::iterator it = ready_evm_events.begin();
                     it != ready_evm_events.end(); it++
#ifdef DEBUG
                         , i++
#endif
                    )
                {
                    Fd fd(*it);
#ifdef DEBUG
                    if (!fd)
                    {
                        PS_LOG_ERR("fd is NULL");
                        continue;
                    }
#endif

                    const Tag tag(EventMethFns::getEmEventUserData(fd));
                    Event event(tag);
                    event.flags = epoll_fd->toNotifyOn(fd); // uses fd's ready_flags
                    PS_LOG_DBG_FD_AND_NOTIFY;
                    events.push_back(event);

                    // fd's ready_flags have been transferred to event.flags
                    EventMethFns::resetEmEventReadyFlags(fd);
                }
            }
            } // end of "try {"
            catch(...)
            {
                PS_LOG_ERR("Throw while polling");
            }

            // unlockInterestMutexIfLocked must be called after
            // getReadyEmEvents, and after we have finished processing the
            // ready_evm_events set. Leaving the mutex locked to this point
            // prevents any other thread closing/invalidating an Fd in the
            // ready_evm_events set while we're processing the set above.
            epoll_fd->unlockInterestMutexIfLocked();

            if (ready_evs <= 0)
                return (ready_evs);

            if (ready_evm_events.empty())
                return (0); // 0 FDs

            return((int)events.size());

#else // not ifdef _USE_LIBEVENT

            struct epoll_event evs[Const::MaxEvents];

            int ready_fds = -1;
            do
            {
                ready_fds = ::epoll_wait(epoll_fd, evs, Const::MaxEvents,
                                         static_cast<int>(timeout.count()));
                PS_LOG_DEBUG_ARGS("done epoll_wait on fd %" PIST_QUOTE(PS_FD_PRNTFCD),
                                  epoll_fd);
            } while (ready_fds < 0 && errno == EINTR);
            PS_LOG_DEBUG_ARGS("while loop done for epoll_wait on fd %" PIST_QUOTE(PS_FD_PRNTFCD) ", ready_fds %d",
                              epoll_fd, ready_fds);

            for (int i = 0; i < ready_fds; ++i)
            {
                const struct epoll_event* ev = evs + i;

                const Tag tag(ev->data.u64);

                Event event(tag);
                event.flags = toNotifyOn(ev->events);
                PS_LOG_DBG_FD_AND_NOTIFY;
                events.push_back(event);
            }

            return ready_fds;
#endif
        }

#ifdef _USE_LIBEVENT
        // static method
        Fd Epoll::em_event_new(em_socket_t actual_fd, // file desc, signal, or -1
                               short flags, // EVM_... flags
                               // For setfd and setfl arg:
                               //   Zero or pos number - set flags to value of
                               //   arg, and clear any other flags
                               //   F_SETFDL_NOTHING - change nothing
                               //   Other neg num - set flags that are set in
                               //   (0 - arg), but don't clear any flags
                               int f_setfd_flags, // e.g. FD_CLOEXEC
                               int f_setfl_flags // e.g. O_NONBLOCK
        )
        {
            return (EventMethFns::em_event_new(actual_fd, flags,
                                                      f_setfd_flags, f_setfl_flags));
        }

        Fd Epoll::em_timer_new(clockid_t clock_id,
                               // For setfd and setfl arg:
                               //   F_SETFDL_NOTHING - change nothing
                               //   Zero or pos number that is not
                               //   F_SETFDL_NOTHING - set flags to value of
                               //   arg, and clear any other flags
                               //   Neg number that is not F_SETFDL_NOTHING
                               //   - set flags that are set in (0 - arg),
                               //   but don't clear any flags
                               int f_setfd_flags, // e.g. FD_CLOEXEC
                               int f_setfl_flags) // e.g. O_NONBLOCK
        {
            if (!epoll_fd)
                throw std::runtime_error("epoll_fd null");

            return (EventMethFns::em_timer_new(clock_id,
                                               f_setfd_flags, f_setfl_flags,
                                               epoll_fd.get()));
        }

        // For "eventfd-style" descriptors
        // Note that FdEventFd does not have an "actual fd" that the caller can
        // access; the caller must use FdEventFd's member functions instead
        FdEventFd Epoll::em_eventfd_new(unsigned int initval,
                                        int f_setfd_flags, // e.g. FD_CLOEXEC
                                        int f_setfl_flags) // e.g. O_NONBLOCK
        {
            return (EventMethFns::em_eventfd_new(initval,
                                                        f_setfd_flags, f_setfl_flags));
        }

#endif // of ifdef _USE_LIBEVENT

#ifndef _USE_LIBEVENT

        int Epoll::toEpollEvents(const Flags<NotifyOn>& interest)
        {
            int events = 0;

            if (interest.hasFlag(NotifyOn::Read))
                events |= EPOLLIN;
            if (interest.hasFlag(NotifyOn::Write))
                events |= EPOLLOUT;
            if (interest.hasFlag(NotifyOn::Hangup))
                events |= EPOLLHUP;
            if (interest.hasFlag(NotifyOn::Shutdown))
                events |= EPOLLRDHUP;

            return events;
        }

        Flags<NotifyOn> Epoll::toNotifyOn(int events)
        {
            Flags<NotifyOn> flags;

            if (events & EPOLLIN)
                flags.setFlag(NotifyOn::Read);
            if (events & EPOLLOUT)
                flags.setFlag(NotifyOn::Write);
            if (events & EPOLLHUP)
                flags.setFlag(NotifyOn::Hangup);
            if (events & EPOLLRDHUP)
            {
                flags.setFlag(NotifyOn::Shutdown);
            }

            return flags;
        }

#endif

    } // namespace Polling

    NotifyFd::NotifyFd()
        : event_fd(PS_FD_EMPTY)
    { }

    NotifyFd::~NotifyFd()
    {
        if (event_fd != PS_FD_EMPTY)
        {
            CLOSE_FD(event_fd);
            event_fd = PS_FD_EMPTY;
        }
    }

    Polling::Tag NotifyFd::bind(Polling::Epoll& poller)
    {
#ifdef _USE_LIBEVENT
        FdEventFd emefd = TRY_NULL_RET(Polling::Epoll::em_eventfd_new(
            0, FD_CLOEXEC, O_NONBLOCK));

        event_fd = EventMethFns::getAsEmEvent(emefd);
#else
        event_fd = TRY_RET(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
#endif

        Polling::Tag tag(event_fd);

        PS_LOG_DEBUG_ARGS("Add read fd %" PIST_QUOTE(PS_FD_PRNTFCD), event_fd);
        poller.addFd(event_fd, Flags<Polling::NotifyOn>(Polling::NotifyOn::Read), tag,
                     Polling::Mode::Edge);
        return tag;
    }

    void NotifyFd::unbind(Polling::Epoll& poller)
    {
        if (event_fd != PS_FD_EMPTY)
        {
            PS_LOG_DEBUG_ARGS("Remove and close event_fd %" PIST_QUOTE(PS_FD_PRNTFCD), event_fd);

            poller.removeFd(event_fd);
            CLOSE_FD(event_fd);
            event_fd = PS_FD_EMPTY;
        }
    }

    bool NotifyFd::isBound() const
    {
        return (event_fd != PS_FD_EMPTY);
    }

    Polling::Tag NotifyFd::tag() const { return Polling::Tag(event_fd); }

    void NotifyFd::notify() const
    {
        PS_TIMEDBG_START_CURLY;

        if (!isBound())
            throw std::runtime_error("Can not notify an unbound fd");

        uint64_t val = 1;
        TRY(WRITE_EFD(event_fd, val));
    }

    void NotifyFd::read() const
    {
        PS_TIMEDBG_START_THIS;

        if (!isBound())
            throw std::runtime_error("Can not read an unbound fd");

        uint64_t val = 0;
        TRY(READ_EFD(event_fd, &val));
    }

    bool NotifyFd::tryRead() const
    {
        PS_TIMEDBG_START_THIS;

        if (!isBound())
            throw std::runtime_error("Can not try to read if unbound");

        uint64_t val = 0;
        int res      = TRY_RET(READ_EFD(event_fd, &val));
#ifdef DEBUG
        if (res != 0) // 0 is success
            PS_LOG_DEBUG_ARGS("FdEventFd %p read fail", event_fd);
#endif

        if (res != 0) // 0 is success
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return false;
            throw std::runtime_error("Failed to read eventfd");
        }

        return true;
    }

} // namespace Pistache
