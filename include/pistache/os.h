/*
 * SPDX-FileCopyrightText: 2015 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* os.h
   Mathieu Stefani, 13 August 2015

   Operating system specific functions
*/

#pragma once

#include <pistache/common.h>
#include <pistache/config.h>
#include <pistache/eventmeth.h>
#include <pistache/flags.h>

#include <bitset>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

#include <sched.h>

namespace Pistache
{
    // Note: Fd is defined in eventmeth.h

    uint hardware_concurrency();
    bool make_non_blocking(int fd);

    class CpuSet
    {
    public:
        static constexpr size_t Size = 1024;

        CpuSet();
        explicit CpuSet(std::initializer_list<size_t> cpus);

        void clear();
        CpuSet& set(size_t cpu);
        CpuSet& unset(size_t cpu);

        CpuSet& set(std::initializer_list<size_t> cpus);
        CpuSet& unset(std::initializer_list<size_t> cpus);

        CpuSet& setRange(size_t begin, size_t end);
        CpuSet& unsetRange(size_t begin, size_t end);

        bool isSet(size_t cpu) const;
        size_t count() const;

#ifdef _POSIX_C_SOURCE
        cpu_set_t toPosix() const;
#endif

    private:
        std::bitset<Size> bits;
    };

    namespace Polling
    {

        enum class Mode { Level,
                          Edge };

        enum class NotifyOn {
            None = 0,

            Read     = 1,
            Write    = Read << 1,
            Hangup   = Read << 2,
            Shutdown = Read << 3
        };

        DECLARE_FLAGS_OPERATORS(NotifyOn)

#ifdef _USE_LIBEVENT
        using TagValue      = Fd;
        using TagValueConst = const Fd;
#define TAG_VALUE_EMPTY NULL
#else
        using TagValue      = uint64_t;
        using TagValueConst = uint64_t;
#endif

        struct Tag
        {
            friend class Epoll;

            explicit constexpr Tag(TagValue value)
                : value_(value)
            { }

            constexpr TagValue value() const { return value_; }
            uint64_t valueU64() const { return ((uint64_t)value_); }
#ifndef _USE_LIBEVENT
            constexpr
#endif
                uint64_t
                actualFdU64Value() const
            {
#ifdef _USE_LIBEVENT
                if (value_ == NULL)
                    return ((uint64_t)((int)-1));
                em_socket_t actual_fd = GET_ACTUAL_FD(value_);
                return ((uint64_t)actual_fd);
#else
                return (value_);
#endif
            }

            friend constexpr bool operator==(Tag lhs, Tag rhs);

        private:
            TagValue value_;
        };

        inline constexpr bool operator==(Tag lhs, Tag rhs)
        {
            return lhs.value_ == rhs.value_;
        }

        struct Event
        {
            explicit Event(Tag _tag);

            Flags<NotifyOn> flags;
            Tag tag;
        };

        class Epoll
        {
        public:
            Epoll();
            ~Epoll();

            void addFd(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode = Mode::Level);
            void addFdOneShot(Fd fd, Flags<NotifyOn> interest, Tag tag,
                              Mode mode = Mode::Level);

            void removeFd(Fd fd);
            void rearmFd(Fd fd, Flags<NotifyOn> interest, Tag tag,
                         Mode mode = Mode::Level);

            int poll(std::vector<Event>& events, const std::chrono::milliseconds timeout = std::chrono::milliseconds(-1)) const;

            // reg_unreg_mutex_ must be locked for a call to poll(...) and
            // remain locked while the caller handles any returned events, to
            // prevent this poller being unregistered while the handling is
            // going on (see also unregisterPoller, plus the long comment for
            // reactor_ in class Handler)
            std::mutex reg_unreg_mutex_;

#ifdef _USE_LIBEVENT
            static Fd em_event_new(
                em_socket_t actual_fd, // file desc, signal, or -1
                short flags, // EVM_... flags
                // For setfd and setfl arg:
                //   F_SETFDL_NOTHING - change nothing
                //   Zero or pos number that is not
                //   F_SETFDL_NOTHING - set flags to value of arg,
                //   and clear any other flags
                //   Neg number that is not F_SETFDL_NOTHING - set
                //   flags that are set in (0 - arg), but don't
                //   clear any flags
                int f_setfd_flags, // e.g. FD_CLOEXEC
                int f_setfl_flags // e.g. O_NONBLOCK
            );

            Fd em_timer_new(clockid_t clock_id,
                            // For setfd and setfl arg:
                            //   F_SETFDL_NOTHING - change nothing
                            //   Zero or pos number that is not
                            //   F_SETFDL_NOTHING - set flags to value of
                            //   arg, and clear any other flags
                            //   Neg number that is not F_SETFDL_NOTHING
                            //   - set flags that are set in (0 - arg),
                            //   but don't clear any flags
                            int f_setfd_flags, // e.g. FD_CLOEXEC
                            int f_setfl_flags); // e.g. O_NONBLOCK

            // For "eventfd-style" descriptors
            // Note that FdEventFd does not have an "actual fd" that the caller
            // can access; the caller must use FdEventFd's member functions
            // instead
            static FdEventFd em_eventfd_new(unsigned int initval,
                                            int f_setfd_flags, // e.g. FD_CLOEXEC
                                            int f_setfl_flags); // e.g. O_NONBLOCK

            std::shared_ptr<EventMethEpollEquiv> getEventMethEpollEquiv()
            {
                return (epoll_fd);
            }
#endif

        private:
#ifndef _USE_LIBEVENT
            static int toEpollEvents(const Flags<NotifyOn>& interest);
            static Flags<NotifyOn> toNotifyOn(int events);
#endif

#ifdef _USE_LIBEVENT
            std::shared_ptr<EventMethEpollEquiv> epoll_fd;
#else
            Fd epoll_fd;
#endif
        };

    } // namespace Polling

    class NotifyFd
    {
    public:
        NotifyFd();
        ~NotifyFd();

        Polling::Tag bind(Polling::Epoll& poller);
        void unbind(Polling::Epoll& poller);

        bool isBound() const;

        Polling::Tag tag() const;

        void notify() const;

        void read() const;
        bool tryRead() const;

    private:
        Fd event_fd;
    };

} // namespace Pistache
