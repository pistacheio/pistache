/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Event Method
// eventmeth.h

#ifndef _EVENTMETH_H_
#define _EVENTMETH_H_

/* ------------------------------------------------------------------------- */

#include <pistache/emosandlibevdefs.h>

// Note: eventmeth wraps and abstracts capabilities of the libevent library,
// providing interfaces to pistache that are similar to the epoll, eventfd and
// timerfd_* capabilities seen natively in Linux.

/* ------------------------------------------------------------------------- */

// A type wide enough to hold the output of "socket()" or "accept()".  On
// Windows, this is an intptr_t; elsewhere, it is an int.
// Note: Mapped directly from evutil_socket_t in libevent's event2/util.h
#ifdef _WIN32
#define em_socket_t SOCKET
#else
#define em_socket_t int
#endif

#ifndef _USE_LIBEVENT
#include <sys/eventfd.h>
#endif

#ifdef _USE_LIBEVENT
#define GET_ACTUAL_FD(__ev__) EventMethFns::getActualFd(__ev__)
#else
#define GET_ACTUAL_FD(__fd__) __fd__
#endif

#ifdef _USE_LIBEVENT
#define WRITE_FD(__fd__, __buf__, __count__)    \
    Pistache::EventMethFns::write(__fd__, __buf__, __count__)
#define READ_FD(__fd__, __buf__, __count__)     \
    Pistache::EventMethFns::read(__fd__, __buf__, __count__)
#else
#define WRITE_FD(__fd__, __buf__, __count__)    \
    ::write(__fd__, __buf__, __count__)
#define READ_FD(__fd__, __buf__, __count__)     \
    ::read(__fd__, __buf__, __count__)
#endif

// WRITE_EFD and READ_EFD are for eventfd/EmEventFd Fds
// They return 0 on success, and -1 on fail
  
#ifdef _USE_LIBEVENT
// Returns -1 if __fd__ not EmEventFd
#define WRITE_EFD(__fd__, __val__)              \
    ((Pistache::EventMethFns::writeEfd(__fd__, __val__) ==       \
                                                    sizeof(uint64_t)) ? 0 : -1)

#define READ_EFD(__fd__, __val_ptr__)           \
    ((Pistache::EventMethFns::readEfd(__fd__, __val_ptr__) ==    \
                                                    sizeof(uint64_t)) ? 0 : -1)

#elif defined(__linux__) && defined(__GNUC__)

#define WRITE_EFD(__fd__, __val__)              \
    ::eventfd_write(__fd__, __val__)
#define READ_EFD(__fd__, __val_ptr__)           \
    ::eventfd_read(__fd__, __val_ptr__)

#else

#define WRITE_EFD(__fd__, __val__)              \
    ((::write(__fd__, &__val__, sizeof(__val__)) == sizeof(uint64_t)) ? 0 : -1)

#define READ_EFD(__fd__, __val_ptr__)                                   \
    ((::read(__fd__, __val_ptr__, sizeof(*__val_ptr__)) ==              \
                                                    sizeof(uint64_t)) ? 0 : -1)
#endif


#ifdef _USE_LIBEVENT
// PS_FD_PRNTFCD is the printf code for printing Fd (typically used in debug
// messages)
// E.g.
//   printf("Fd is %" PIST_QUOTE(PS_FD_PRNTFCD), fd);
#define PS_FD_PRNTFCD p
#else
#define PS_FD_PRNTFCD d
#endif

#ifdef _USE_LIBEVENT
// Note - closeEvent calls delete __ev__;
#define CLOSE_FD(__ev__)                                   \
    {                                                      \
        if (__ev__ != PS_FD_EMPTY)                         \
        {                                                  \
            EventMethFns::closeEvent(__ev__);              \
            __ev__ = PS_FD_EMPTY;                          \
        }                                                  \
    }
#else
#define CLOSE_FD(__fd__)                                        \
    {                                                           \
        if (__fd__ != PS_FD_EMPTY)                              \
        {                                                       \
            ::close(__fd__);                                    \
            PS_LOG_DEBUG_ARGS("::close actual_fd %d", __fd__);  \
                                                                \
            __fd__ = PS_FD_EMPTY;                               \
        }                                                       \
    }
#endif

#if defined DEBUG && defined _USE_LIBEVENT
namespace Pistache
{
extern void dbg_log_all_emes();
}
#define DBG_LOG_ALL_EMEVENTS                                \
    {                                                       \
        PS_LOG_DEBUG("Listing EmEvents");                   \
        Pistache::dbg_log_all_emes();                       \
    }
#else
#define DBG_LOG_ALL_EMEVENTS
#endif

#if defined DEBUG && defined _USE_LIBEVENT
  #define LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(__ACTUAL_FD__)                   \
           PS_LOG_DEBUG_ARGS("%s",                                        \
            EventMethFns::getActFdAndFdlFlagsAsStr(__ACTUAL_FD__). \
                                                                 c_str())
#else
  #define LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(__ACTUAL_FD__)
#endif


namespace Pistache
{
    #ifdef _USE_LIBEVENT
      class EmEvent;
      class EmEventFd;
      class EmEventTmrFd;
    
      using Fd = EmEvent *;
      using FdConst = const EmEvent *;

      using FdEventFd = EmEventFd *;
      using FdEventFdConst = const EmEventFd *;

      using FdEventTmrFd = EmEventTmrFd *;
      using FdEventTmrFdConst = const EmEventTmrFd *;

      #define PS_FD_EMPTY NULL
    #else
      using Fd = int;
      using FdConst = int;
      #define PS_FD_EMPTY -1
    #endif
}

/* ------------------------------------------------------------------------- */

#ifdef _USE_LIBEVENT

/* ------------------------------------------------------------------------- */


#include <pistache/common.h>
#include <pistache/flags.h>

#include <mutex>
#include <set>
#include <map>
#include <condition_variable>

#include <fcntl.h> // For FD_CLOEXEC and O_NONBLOCK

/* ------------------------------------------------------------------------- */

namespace Pistache
{
    namespace Polling { enum class NotifyOn; } // NotifyOn used by toEvEvents

    // These flags correspond to libevent's EV_* flags
    #define EVM_TIMEOUT      0x01
    #define EVM_READ         0x02
    #define EVM_WRITE        0x04
    #define EVM_SIGNAL       0x08
    #define EVM_PERSIST      0x10
    #define EVM_ET           0x20
    // FINALIZE is internal to eventmeth, not to be used by eventmeth user
    #define EVM_CLOSED       0x80

    enum class EvCtlAction
    {
        Add = 1,
        Mod = 2, // rearm
        Del = 3
    };

    enum EmEventType
    {
        EmEvNone      = 0,
        EmEvReg       = 1,
        EmEvEventFd   = 2,
        EmEvTimer     = 3
    };

    class EventMethEpollEquiv;
    class EventMethEpollEquivImpl;

    // EventMethFns contains non-member functions that act on EmEvent,
    // EmEventFd and EmEventTmrFd, which, when outside the scope of
    // eventmet.cc, are opaque types. The EventMethFns also reference
    // EventMethEpollEquiv in somce cases.
    namespace EventMethFns
    {
        // size is a hint as to how many FDs to be monitored
        std::shared_ptr<EventMethEpollEquiv> create(int size);


        #define F_SETFDL_NOTHING ((int)((unsigned) 0x8A82))
        Fd em_event_new(em_socket_t actual_fd,//file desc, signal, or -1
                        short flags, // EVM_... flags
                        // For setfd and setfl arg:
                        //   F_SETFDL_NOTHING - change nothing
                        //   Zero or pos number that is not F_SETFDL_NOTHING -
                        //   set flags to value of arg, and clear any other
                        //   flags
                        //   Neg number that is not F_SETFDL_NOTHING - set
                        //   flags that are set in (0 - arg), but don't clear
                        //   any flags
                        int f_setfd_flags, // e.g. FD_CLOEXEC
                        int f_setfl_flags  // e.g. O_NONBLOCK
            );

        // If emee is NULL here, it will need to be supplied when settime is
        // called
        Fd em_timer_new(clockid_t clock_id,
                               // For setfd and setfl arg:
                               //   F_SETFDL_NOTHING - change nothing
                               //   Zero or pos number that is not
                               //   F_SETFDL_NOTHING - set flags to value of
                               //   arg, and clear any other flags
                               //   Neg number that is not F_SETFDL_NOTHING
                               //   - set flags that are set in (0 - arg),
                               //   but don't clear any flags
                               int f_setfd_flags,   // e.g. FD_CLOEXEC
                               int f_setfl_flags,  // e.g. O_NONBLOCK
                               EventMethEpollEquiv * emee/*may be NULL*/);

        // For "eventfd-style" descriptors
        // Note that FdEventFd does not have an "actual fd" that the caller can
        // access; the caller must use FdEventFd's member functions instead
        FdEventFd em_eventfd_new(unsigned int initval,
                                 int f_setfd_flags, // e.g. FD_CLOEXEC
                                 int f_setfl_flags);  // e.g. O_NONBLOCK

        // Returns 0 for success, on error -1 with errno set
        // Will add/remove from interest_ if appropriate
        int ctl(EvCtlAction op, // add, mod, or del
                       EventMethEpollEquiv * epoll_equiv,
                       Fd event, // libevent event
                       short events, // bitmask per epoll_ctl (EVM_... events)
                       std::chrono::milliseconds * timeval_cptr);

        // rets 0 on success, -1 error
        int closeEvent(EmEvent * em_event);
        // See also CLOSE_FD macro

        // Returns emee if emee is in emee_cptr_set_, or NULL
        // otherwise. emee_cptr_set_mutex_ is locked inside the function.
        EventMethEpollEquiv * getEventMethEpollEquivFromEmeeSet(
                                                   EventMethEpollEquiv * emee);
        
        int getActualFd(const EmEvent * em_event);

        // efd should be a pointer to EmEventFd - does dynamic cast
        ssize_t writeEfd(EmEvent * efd, const uint64_t val);
        ssize_t readEfd(EmEvent * efd, uint64_t * val_out_ptr);

        ssize_t read(EmEvent * fd, void * buf, size_t count);
        ssize_t write(EmEvent * fd, const void * buf, size_t count);

        EmEvent * getAsEmEvent(EmEventFd * efd);

        uint64_t getEmEventUserDataUi64(const EmEvent * fd);
        Fd getEmEventUserData(const EmEvent * fd);
        void setEmEventUserData(EmEvent * fd, uint64_t user_data);
        void setEmEventUserData(EmEvent * fd, Fd user_data);

        // For EmEventTmrFd, settime is analagous to timerfd_settime in linux
        // 
        // The linux flags TFD_TIMER_ABSTIME and TFD_TIMER_CANCEL_ON_SET are
        // not supported
        //
        // Since pistache doesn't use the "struct itimerspec * old_value"
        // feature of timerfd_settime, we haven't implemented that feature.
        // 
        // If the EventMethEpollEquiv was not specified already (e.g. at
        // make_new), the it must be specified here
        // 
        // Note: settime is in EmEvent rather than solely in EmEventTmrFd since
        // any kind of event may have a timeout set, not only timer events
        int setEmEventTime(EmEvent * fd,
                            const std::chrono::milliseconds * new_timeval_cptr,
                            EventMethEpollEquiv * emee = NULL/*may be NULL*/);

        EmEventType getEmEventType(EmEvent * fd);

        void resetEmEventReadyFlags(EmEvent * fd);

        EventMethEpollEquivImpl * getEMEEImpl(
                                    EventMethEpollEquiv * emee/*may be NULL*/);

    #ifdef DEBUG
        std::string getActFdAndFdlFlagsAsStr(int actual_fd);
        // See also macro LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(__ACTUAL_FD__)
    #endif

    #ifdef DEBUG
        int getEmEventCount();
        int getLibeventEventCount();
        int getEventMethEpollEquivCount();
        int getEventMethBaseCount();
        int getWaitThenGetAndEmptyReadyEvsCount();
    #endif
    } // namespace EventMethFns

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

    class EventMethEpollEquiv
    { // See man epoll, epoll_create, epoll_ctl, epl_wait
        
    public:
        
        // Add to interest list
        // Returns 0 for success, on error -1 with errno set
        int ctl(EvCtlAction op, // add, mod, or del
                Fd em_event,
                short     events, // bitmask of EVM_... events
                std::chrono::milliseconds * timeval_cptr);

        // unlockInterestMutexIfLocked is used only in conjunction with
        // getReadyEmEvents
        void unlockInterestMutexIfLocked();

        // Waits (if needed) until events are ready, then sets the _out set to
        // be equal to ready events, and empties the list of ready events
        // "timeout" is in milliseconds, or -1 means wait indefinitely
        // Returns number of ready events being returned; or 0 if timed-out
        // without an event becoming ready; or -1, with errno set, on error
        // 
        // NOTE: Caller must call unlockInterestMutexIfLocked after
        // getReadyEmEvents has returned and after the caller has finished
        // processing any Fds in ready_evm_events_out. getReadyEmEvents returns
        // with the interest mutex locked (or it may be locked) to ensure that
        // another thread cannot close an Fd in the interest list, given that
        // that Fd may also be in returned ready_evm_events_out and could be
        // invalidated by the close before the caller could get to it.
        int getReadyEmEvents(int timeout, std::set<Fd> & ready_evm_events_out);

        // EvEvents are some combination of EVM_TIMEOUT, EVM_READ, EVM_WRITE,
        // EVM_SIGNAL, EVM_PERSIST, EVM_ET, EVM_CLOSED

        int toEvEvents(const Flags<Polling::NotifyOn>& interest);
        Flags<Polling::NotifyOn> toNotifyOn(Fd fd); // uses fd->ready_flags

        ~EventMethEpollEquiv();

    private:
        // Allow create to call the constructor
        friend std::shared_ptr<EventMethEpollEquiv> EventMethFns::create(int);
        
        EventMethEpollEquiv(int size);

        friend EventMethEpollEquivImpl * EventMethFns::getEMEEImpl(
                                                        EventMethEpollEquiv *);
        
        std::unique_ptr<EventMethEpollEquivImpl> impl_;
    };

    // To enable to_string of an Fd
    // Note: Make sure to include "using std::to_string;" in the code, so
    // compiler still knows what to_string to use in the event Fd is an int
    std::string to_string(const EmEvent * eme);

} // namespace Pistache
#endif // ifdef _USE_LIBEVENT

/* ------------------------------------------------------------------------- */

#endif // ifndef _EVENTMETH_H_
