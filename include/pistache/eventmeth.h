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

#ifdef PISTACHE_FORCE_LIBEVENT

// Force libevent even for Linux
#define _USE_LIBEVENT 1

// _USE_LIBEVENT_LIKE_APPLE not only forces libevent, but even in Linux causes
// the code to be as similar as possible to the way it is for __APPLE__
// (e.g. whereever possible, even on Linux it uses solely OS calls that are
// available on Apple)
//
// Can comment out if not wanted
#define _USE_LIBEVENT_LIKE_APPLE 1

#endif // ifdef PISTACHE_FORCE_LIBEVENT

#ifdef _USE_LIBEVENT_LIKE_APPLE
  #ifndef _USE_LIBEVENT
    #define _USE_LIBEVENT 1
  #endif
#endif

#ifdef __APPLE__
  #ifndef _USE_LIBEVENT
    #define _USE_LIBEVENT 1
  #endif
  #ifndef _USE_LIBEVENT_LIKE_APPLE
    #define _USE_LIBEVENT_LIKE_APPLE 1
  #endif
#elif defined(_WIN32) // Defined for both 32-bit and 64-bit environments
  #define _USE_LIBEVENT 1
#endif

#ifndef _USE_LIBEVENT
  // Note: sys/eventfd.h is linux-only / POSIX only
  #include <sys/eventfd.h>
#endif
// Note: libevent's event.h should NOT be included in this file, it is included
// only in the eventmeth CPP file. eventmeth.h users should not be using
// libevent directly, since eventmeth is our wrapper for libevent

/* ------------------------------------------------------------------------- */

// A type wide enough to hold the output of "socket()" or "accept()".  On
// Windows, this is an intptr_t; elsewhere, it is an int.
// Note: Mapped directly from evutil_socket_t in libevent's event2/util.h
#ifdef _WIN32
#define em_socket_t intptr_t
#else
#define em_socket_t int
#endif


#ifdef _USE_LIBEVENT
#define GET_ACTUAL_FD(__ev__) EventMethEpollEquiv::getActualFd(__ev__)
#else
#define GET_ACTUAL_FD(__fd__) __fd__
#endif

#ifdef _USE_LIBEVENT
#define WRITE_FD(__fd__, __buf__, __count__)    \
    Pistache::EventMethEpollEquiv::write(__fd__, __buf__, __count__)
#define READ_FD(__fd__, __buf__, __count__)     \
    Pistache::EventMethEpollEquiv::read(__fd__, __buf__, __count__)
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
    ((Pistache::EventMethEpollEquiv::writeEfd(__fd__, __val__) ==       \
                                                    sizeof(uint64_t)) ? 0 : -1)

#define READ_EFD(__fd__, __val_ptr__)           \
    ((Pistache::EventMethEpollEquiv::readEfd(__fd__, __val_ptr__) ==    \
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
#define CLOSE_FD(__ev__)                                \
    {                                                   \
        if (__ev__ != FD_EMPTY)                         \
        {                                               \
            EventMethEpollEquiv::closeEvent(__ev__);    \
            __ev__ = FD_EMPTY;                          \
        }                                               \
    }
#else
#define CLOSE_FD(__fd__)                                        \
    {                                                           \
        if (__fd__ != FD_EMPTY)                                 \
        {                                                       \
            ::close(__fd__);                                    \
            PS_LOG_DEBUG_ARGS("::close actual_fd %d", __fd__);  \
                                                                \
            __fd__ = FD_EMPTY;                                  \
        }                                                       \
    }
#endif

#ifndef PIST_QUOTE // used for placing quote signs around a preprocessor parm
  #define PIST_Q(x) #x
  #define PIST_QUOTE(x) PIST_Q(x)
#endif

#if defined DEBUG && defined _USE_LIBEVENT
  #define LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(__ACTUAL_FD__)                   \
           PS_LOG_DEBUG_ARGS("%s",                                        \
            EventMethEpollEquiv::getActFdAndFdlFlagsAsStr(__ACTUAL_FD__). \
                                                                 c_str())
#else
  #define LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(__ACTUAL_FD__)
#endif


namespace Pistache
{
    #ifdef _USE_LIBEVENT
      class EmEvent;
      class EmEventCtr;
      class EmEventFd;
      class EmEventTmrFd;
    
      using Fd = EmEvent *;
      using FdConst = const EmEvent *;

      using FdEventFd = EmEventFd *;
      using FdEventFdConst = const EmEventFd *;

      using FdEventTmrFd = EmEventTmrFd *;
      using FdEventTmrFdConst = const EmEventTmrFd *;

      #define FD_EMPTY NULL
    #else
      using Fd = int;
      using FdConst = int;
      #define FD_EMPTY -1
    #endif
}

/* ------------------------------------------------------------------------- */

#ifdef _USE_LIBEVENT

/* ------------------------------------------------------------------------- */


#include <pistache/common.h>
#include <pistache/flags.h>

#include <event2/thread.h>

#include <mutex>
#include <set>
#include <map>
#include <condition_variable>

#include <fcntl.h> // For FD_CLOEXEC and O_NONBLOCK

/* ------------------------------------------------------------------------- */

struct event; // treat as private to libevent

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
    #define EVM_FINALIZE     0x40
    #define EVM_CLOSED       0x80

    class EventMethBase;

    class EmEventTmrFd;
    class EmEventFd;
    class EmEventCtr;
    class EmEvent;
    
    enum EventCtlAction
    {
        EvCtlAdd = 1,
        EvCtlMod = 2, // rearm
        EvCtlDel = 3
    };

    enum EmEventType
    {
        EmEvNone      = 0,
        EmEvReg       = 1,
        EmEvEventFd   = 2,
        EmEvTimer     = 3
    };
        
    class EventMethEpollEquiv
    { // See man epoll, epoll_create, epoll_ctl, epl_wait
    public:
        // size is a hint as to how many FDs to be monitored
        static std::shared_ptr<EventMethEpollEquiv> create(int size);

        int getEventBaseFeatures();

        // Add to interest list
        // Returns 0 for success, on error -1 with errno set
        int ctl(EventCtlAction op, // add, mod, or del
                Fd em_event,
                short     events, // bitmask of EVM_... events
                std::chrono::milliseconds * timeval_cptr);

        #define F_SETFDL_NOTHING ((int)((unsigned) 0x8A82))
        static Fd em_event_new(em_socket_t actual_fd,//file desc, signal, or -1
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
        static Fd em_timer_new(clockid_t clock_id,
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
        static FdEventFd em_eventfd_new(unsigned int initval,
                                 int f_setfd_flags, // e.g. FD_CLOEXEC
                                 int f_setfl_flags);  // e.g. O_NONBLOCK

        // Waits (if needed) until events are ready, then sets the _out set to
        // be equal to ready events, and empties the list of ready events
        // "timeout" is in milliseconds, or -1 means wait indefinitely
        // Returns number of ready events being returned; or 0 if timed-out
        // without an event becoming ready; or -1, with errno set, on error
        int waitThenGetAndEmptyReadyEvs(int timeout,
                                        std::set<Fd> & ready_evm_events_out);

        // EvEvents are some combination of EVM_TIMEOUT, EVM_READ, EVM_WRITE,
        // EVM_SIGNAL, EVM_PERSIST, EVM_ET, EVM_FINALIZE, EVM_CLOSED
        
        int toEvEvents(const Flags<Polling::NotifyOn>& interest);

        Flags<Polling::NotifyOn> toNotifyOn(Fd fd); // uses fd->ready_flags


        // Returns 0 for success, on error -1 with errno set
        // Will add/remove from interest_ if appropriate
        static int ctl(EventCtlAction op, // add, mod, or del
                       EventMethEpollEquiv * epoll_equiv,
                       Fd event, // libevent event
                       short events, // bitmask per epoll_ctl (EVM_... events)
                       std::chrono::milliseconds * timeval_cptr);

        // rets 0 on success, -1 error
        static int closeEvent(EmEvent * em_event);
        // See also CLOSE_FD macro

        ~EventMethEpollEquiv();

        // Returns emee if emee is in emee_cptr_set_, or NULL
        // otherwise. emee_cptr_set_mutex_ is locked inside the function.
        static EventMethEpollEquiv * getEventMethEpollEquivFromEmeeSet(
                                                   EventMethEpollEquiv * emee);
        
        static int getTcpProtNum(); // As per getprotobyname("tcp")

        void handleEventCallback(EmEvent * em_event,
                                 short ev_flags); // One or more EVM_* flags

    public:
        static int getActualFd(const EmEvent * em_event);

        // efd should be a pointer to EmEventFd - does dynamic cast
        static ssize_t writeEfd(EmEvent * efd, const uint64_t val);
        static ssize_t readEfd(EmEvent * efd, uint64_t * val_out_ptr);

        static ssize_t read(EmEvent * fd, void * buf, size_t count);
        static ssize_t write(EmEvent * fd, const void * buf, size_t count);

        static EmEvent * getAsEmEvent(EmEventFd * efd);

        static uint64_t getEmEventUserDataUi64(const EmEvent * fd);
        static Fd getEmEventUserData(const EmEvent * fd);
        static void setEmEventUserData(EmEvent * fd, uint64_t user_data);
        static void setEmEventUserData(EmEvent * fd, Fd user_data);

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
        static int setEmEventTime(EmEvent * fd,
                            const std::chrono::milliseconds * new_timeval_cptr,
                            EventMethEpollEquiv * emee = NULL/*may be NULL*/);

        static EmEventType getEmEventType(EmEvent * fd);

        static void resetEmEventReadyFlags(EmEvent * fd);

    public:
        // findEmEventInAnInterestSet scans the interest_ set of all the
        // EventMethEpollEquiv looking for an EMEvent pointer that matches
        // arg. If one if found, that matching EmEvent pointer is returned and
        // *epoll_equiv_cptr_out is set to point to the EventMethEpollEquiv
        // whose interest_ was found to contain the match; otherwise, NULL is
        // returned
        static EmEvent * findEmEventInAnInterestSet(void * arg,
                                 EventMethEpollEquiv * * epoll_equiv_cptr_out);

        // Returns number of removals (0, 1 or 2)
        std::size_t removeFromInterestAndReady(EmEvent * em_event);

        // Note there is a different EventMethBase for each EventMethEpollEquiv
        EventMethBase * getEventMethBase() {return(event_meth_base_.get());}

    #ifdef DEBUG
    public:
        static std::string getActFdAndFdlFlagsAsStr(int actual_fd);
        // See also macro LOG_DEBUG_ACT_FD_AND_FDL_FLAGS(__ACTUAL_FD__)
    #endif

    #ifdef DEBUG
    public:
        static int getEmEventCount();
        static int getLibeventEventCount();
        static int getEventMethEpollEquivCount();
        static int getEventMethBaseCount();
        static int getWaitThenGetAndEmptyReadyEvsCount();
    #endif

    private:
        // size is a hint as to how many FDs to be monitored
        static EventMethEpollEquiv * createCPtr(int size);
        
        EventMethEpollEquiv(EventMethBase * event_meth_base);

        // If found in ready_, returns 1
        // If not found in ready_ but found in interest_, returns 0
        // If found in neither, returns -1
        int removeSpecialTimerFromInterestAndReady(
            EmEvent * loop_timer_eme,
            std::size_t * remaining_ready_size_out_ptr);

        int waitThenGetAndEmptyReadyEvsHelper(int timeout,
                                          std::set<Fd> & ready_evm_events_out);
        
        // returns FD_EMPTY if not found, returns fd if found
        Fd findFdInInterest(Fd fd);
        void addEventToReady(Fd fd, short ev_flags);// One or more EVM_* flags

        #ifdef DEBUG
        void logPendingOrNot();
        #endif

        // Add to interest list
        // Returns 0 for success, on error -1 with errno set
        // If forceEmEventCtlOnly is true, it will not call the ctl() function
        // of classes (like EmEventFd) derived from EmEvent but only the ctl
        // function of EmEvent itself. For internal use only.
        int ctlEx(EventCtlAction op, // add, mod, or del
                  Fd em_event,
                  short     events, // bitmask of EVM_... events
                  std::chrono::milliseconds * timeval_cptr,
                  bool forceEmEventCtlOnly);
        // friend renewEv() so it can call EventMethEpollEquiv::ctlEx
        // Likewise for EmEventTmrFd::settime
        friend class EmEventCtr; // for use of renewEv
        friend class EmEventTmrFd; // for use of settime
        /*
         * !!!!
        friend void EmEventCtr::renewEv();
        friend int EmEventTmrFd::settime(const std::chrono::milliseconds *,
                                         EventMethEpollEquiv * );
        */

        // Note there is a different EventMethBase for each EventMethEpollEquiv
        std::unique_ptr<EventMethBase> event_meth_base_;
        
        // interest list - events that process has asked to be monitored
        std::set<Fd> interest_;
        // ready - members of interest list ready for IO
        std::set<Fd> ready_;

        // If both interest_mutex_ and ready_mutex_ are to be locked, lock
        // interest FIRST
        std::mutex interest_mutex_;
        std::mutex ready_mutex_;

        // Don't access tcp_prot_num directly, use getTcpProtNum()
        static int tcp_prot_num;
        static std::mutex tcp_prot_num_mutex;


    private:
        static std::set<EventMethEpollEquiv *> emee_cptr_set_;
        static std::mutex emee_cptr_set_mutex_;
        // Note: If emee_cptr_set_mutex_ is to be locked together with
        // interest_mutex_, emee_cptr_set_mutex_ must be locked first
        
    };

    // To enable to_string of an Fd
    // Note: Make sure to include "using std::to_string;" in the code, so
    // compiler still knows what to_string to use in the event Fd is an int
    std::string to_string(const EmEvent * eme);

} // namespace Pistache
#endif // ifdef _USE_LIBEVENT

/* ------------------------------------------------------------------------- */

#endif // ifdef _USE_LIBEVENT
