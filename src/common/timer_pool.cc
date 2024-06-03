/*
 * SPDX-FileCopyrightText: 2016 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* timer_pool.cc
   Mathieu Stefani, 09 février 2016

   Implementation of the timer pool
*/

#include <pistache/os.h>
#include <pistache/timer_pool.h>

#include <cassert>

#include <pistache/eventmeth.h>
#include <pistache/pist_quote.h>

#ifndef _USE_LIBEVENT_LIKE_APPLE
// Note: sys/timerfd.h is linux-only (and certainly POSIX only)
#include <sys/timerfd.h>
#endif

namespace Pistache
{

    TimerPool::Entry::Entry()
        : fd_(PS_FD_EMPTY)
        , registered(false)
    {
        state.store(static_cast<uint32_t>(State::Idle));
    }

    TimerPool::Entry::~Entry()
    {
        if (fd_ != PS_FD_EMPTY)
        {
            CLOSE_FD(fd_);
            fd_ = PS_FD_EMPTY;
        }
    }

    Fd TimerPool::Entry::fd() const
    {
        assert(fd_ != PS_FD_EMPTY);

        return fd_;
    }

    void TimerPool::Entry::initialize()
    {
        if (fd_ == PS_FD_EMPTY)
        {
#ifdef _USE_LIBEVENT
            fd_ = TRY_NULL_RET(EventMethFns::em_timer_new(
                CLOCK_MONOTONIC,
                F_SETFDL_NOTHING, O_NONBLOCK,
                NULL /* EventMethEpollEquiv ptr */));
            // The EventMethEpollEquiv ptr gets set
            // later, when
            // TimerPool::Entry::registerReactor is
            // called (the reactor owns the
            // EventMethEpollEquiv)
#else
            fd_ = TRY_RET(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));
#endif
        }
    }

    void TimerPool::Entry::disarm()
    {
        if (fd_ == PS_FD_EMPTY)
        {
            assert(fd_ != PS_FD_EMPTY);
            PS_LOG_DEBUG("fd_ empty");
            throw std::runtime_error("fd_ empty");
        }

#ifdef _USE_LIBEVENT
        TRY(EventMethFns::setEmEventTime(fd_, NULL));
#else
        itimerspec spec;
        spec.it_interval.tv_sec  = 0;
        spec.it_interval.tv_nsec = 0;

        spec.it_value.tv_sec  = 0;
        spec.it_value.tv_nsec = 0;

        TRY(timerfd_settime(fd_, 0, &spec, nullptr));
#endif
    }

    void TimerPool::Entry::registerReactor(const Aio::Reactor::Key& key,
                                           Aio::Reactor* reactor)
    {
        if (!registered)
        {
            PS_LOG_DEBUG_ARGS("Register fd %" PIST_QUOTE(PS_FD_PRNTFCD) " with reactor %p",
                              fd_, reactor);

            reactor->registerFd(key, fd_, Polling::NotifyOn::Read);
            registered = true;
        }
    }

    void TimerPool::Entry::armMs(std::chrono::milliseconds value)
    {
        if (fd_ == PS_FD_EMPTY)
        {
            PS_LOG_DEBUG("fd_ NULL");
            assert(fd_ != PS_FD_EMPTY);
            throw std::runtime_error("fd_ NULL");
        }

#ifdef _USE_LIBEVENT
        TRY(EventMethFns::setEmEventTime(fd_, &value));
#else

        itimerspec spec;
        spec.it_interval.tv_sec  = 0;
        spec.it_interval.tv_nsec = 0;

        if (value.count() < 1000)
        {
            spec.it_value.tv_sec  = 0;
            spec.it_value.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(value).count();
        }
        else
        {
            spec.it_value.tv_sec  = std::chrono::duration_cast<std::chrono::seconds>(value).count();
            spec.it_value.tv_nsec = 0;
        }
        TRY(timerfd_settime(fd_, 0, &spec, nullptr));
#endif
    }

    TimerPool::TimerPool(size_t initialSize)
    {
        for (size_t i = 0; i < initialSize; ++i)
        {
            timers.push_back(std::make_shared<TimerPool::Entry>());
        }
    }

    std::shared_ptr<TimerPool::Entry> TimerPool::pickTimer()
    {
        for (auto& entry : timers)
        {
            auto curState = static_cast<uint32_t>(TimerPool::Entry::State::Idle);
            auto newState = static_cast<uint32_t>(TimerPool::Entry::State::Used);
            if (entry->state.compare_exchange_strong(curState, newState))
            {
                entry->initialize();
                return entry;
            }
        }

        return nullptr;
    }

    void TimerPool::releaseTimer(const std::shared_ptr<Entry>& timer)
    {
        timer->state.store(static_cast<uint32_t>(TimerPool::Entry::State::Idle));
    }

} // namespace Pistache
