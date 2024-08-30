/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pistache/eventmeth.h>
#include <pistache/pist_quote.h>

/* ------------------------------------------------------------------------- */

#ifdef _USE_LIBEVENT

#include <event2/thread.h>

/* ------------------------------------------------------------------------- */
/*
 * Event classes - EmEvent, EmEventCtr, EmEventFd and EmEventTmrFd
 *
 */

struct event; // libevent's event struct

namespace Pistache
{
    class EventMethBase;
    class EmEventCtr;
    
    class EventMethEpollEquivImpl
    {
    public:
        int getEventBaseFeatures();

        // Add to interest list
        // Returns 0 for success, on error -1 with errno set
        int ctl(EvCtlAction op, // add, mod, or del
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
                               EventMethEpollEquivImpl * emee/*may be NULL*/);

        // For "eventfd-style" descriptors
        // Note that FdEventFd does not have an "actual fd" that the caller can
        // access; the caller must use FdEventFd's member functions instead
        static FdEventFd em_eventfd_new(unsigned int initval,
                                 int f_setfd_flags, // e.g. FD_CLOEXEC
                                 int f_setfl_flags);  // e.g. O_NONBLOCK

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
        int getReadyEmEvents(int timeout,
                             std::set<Fd> & ready_evm_events_out);

        // EvEvents are some combination of EVM_TIMEOUT, EVM_READ, EVM_WRITE,
        // EVM_SIGNAL, EVM_PERSIST, EVM_ET, EVM_CLOSED
        
        int toEvEvents(const Flags<Polling::NotifyOn>& interest);

        Flags<Polling::NotifyOn> toNotifyOn(Fd fd); // uses fd->ready_flags


        // Returns 0 for success, on error -1 with errno set
        // Will add/remove from interest_ if appropriate
        static int ctl(EvCtlAction op, // add, mod, or del
                       EventMethEpollEquivImpl * epoll_equiv,
                       Fd event, // libevent event
                       short events, // bitmask per epoll_ctl (EVM_... events)
                       std::chrono::milliseconds * timeval_cptr);

        // rets 0 on success, -1 error
        static int closeEvent(EmEvent * em_event);
        // See also CLOSE_FD macro

        EventMethEpollEquivImpl(int size);
        ~EventMethEpollEquivImpl();

        // Returns emee if emee is in emee_cptr_set_, or NULL
        // otherwise. emee_cptr_set_mutex_ is locked inside the function.
        static EventMethEpollEquivImpl * getEventMethEpollEquivImplFromEmeeSet(
                                               EventMethEpollEquivImpl * emee);
        
        static int getTcpProtNum(); // As per getprotobyname("tcp")

        void handleEventCallback(void * cb_arg,
                                 em_socket_t cb_actual_fd,
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
                         EventMethEpollEquivImpl * emee = NULL/*may be NULL*/);

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
                             EventMethEpollEquivImpl * * epoll_equiv_cptr_out);

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
        // If found in ready_, returns 1
        // If not found in ready_ but found in interest_, returns 0
        // If found in neither, returns -1
        int removeSpecialTimerFromInterestAndReady(
            EmEvent * loop_timer_eme,
            std::size_t * remaining_ready_size_out_ptr);

        int getReadyEmEventsHelper(int timeout,
                                          std::set<Fd> & ready_evm_events_out);
        
        // returns PS_FD_EMPTY if not found, returns fd if found
        Fd findFdInInterest(Fd fd);
        void addEventToReadyInterestAlrdyLocked(Fd fd,
                                    short ev_flags); // One or more EVM_* flags

        #ifdef DEBUG
        void logPendingOrNot();
        #endif

        // Add to interest list
        // Returns 0 for success, on error -1 with errno set
        // If forceEmEventCtlOnly is true, it will not call the ctl() function
        // of classes (like EmEventFd) derived from EmEvent but only the ctl
        // function of EmEvent itself. For internal use only.
        int ctlEx(EvCtlAction op, // add, mod, or del
                  Fd em_event,
                  short     events, // bitmask of EVM_... events
                  std::chrono::milliseconds * timeval_cptr,
                  bool forceEmEventCtlOnly);
        // friend renewEv() so it can call EventMethEpollEquiv::ctlEx
        // Likewise for EmEventTmrFd::settime
        friend class EmEventCtr; // for use of renewEv
        friend class EmEventTmrFd; // for use of settime

        // For use of getReadyEmEventsHelper only
        void lockInterestMutex();

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

        std::mutex int_mut_locked_by_get_ready_em_events_mutex_;
        bool int_mut_locked_by_get_ready_em_events_;

        // Don't access tcp_prot_num directly, use getTcpProtNum()
        static int tcp_prot_num;
        static std::mutex tcp_prot_num_mutex;


    private:
        static std::set<EventMethEpollEquivImpl *> emee_cptr_set_;
        static std::mutex emee_cptr_set_mutex_;
        // Note: If emee_cptr_set_mutex_ is to be locked together with
        // interest_mutex_, emee_cptr_set_mutex_ must be locked first
    };
    
        
    class EmEvent
    {
    public:
        
        static EmEvent * make_new(int actual_fd, short flags,
                                  // For setfd and setfl arg:
                                  //   F_SETFDL_NOTHING - change nothing
                                  //   Zero or pos number that is not
                                  //   F_SETFDL_NOTHING - set flags to value of
                                  //   arg, and clear any other flags
                                  //   Neg number that is not F_SETFDL_NOTHING
                                  //   - set flags that are set in (0 - arg),
                                  //   but don't clear any flags
                                  int f_setfd_flags, // e.g. FD_CLOEXEC
                                  int f_setfl_flags  // e.g. O_NONBLOCK
            );

        // set_timeout can be used to configure the timeout prior to calling
        // ctl/Add. Returns 0 on success.
        int set_timeout(std::chrono::milliseconds * timeval_cptr);

        // For EmEventTmrFd, settime is analagous to timerfd_settime in linux
        // 
        // The linux flags TFD_TIMER_ABSTIME and TFD_TIMER_CANCEL_ON_SET are
        // not supported
        //
        // Since pistache doesn't use the "struct itimerspec * old_value"
        // feature of timerfd_settime, we haven't implemented that feature.
        // 
        // If new_timeval_cptr is NULL or *new_timeval_cptr is all zero, the
        // settime call will reset the timer (again as per timerfd_settime)
        //
        // If the EventMethEpollEquiv was not specified already (e.g. at
        // make_new), the it must be specified here
        // 
        // Note: settime is in EmEvent rather than solely in EmEventTmrFd since
        // any kind of event may have a timeout set, not only timer events
        virtual int settime(const std::chrono::milliseconds* new_timeval_cptr,
                         EventMethEpollEquivImpl * emee = NULL/*may be NULL*/);

        int disarm();
        int close(); // disarms and closes

        // Return -1 if there is no actual file descriptor
        static int getActualFd(const EmEvent * em_ev);
        virtual int getActualFd() const;

        virtual ssize_t read(void * buf, size_t count);
        virtual ssize_t write(const void * buf, size_t count);

        virtual int ctl(
            EvCtlAction op, //add,mod,del
            EventMethEpollEquivImpl * emee,
            short events,     // bitmask of EVM... events
            std::chrono::milliseconds * timeval_cptr);

        // Returns true if "pending" (i.e. has been added in libevent)
        // events is any of EV_TIMEOUT|EV_READ|EV_WRITE|EV_SIGNAL
        // If tv is non-null and event has time out, *tv is set to it
        // Returns true if pending (i.e. added) on events, false otherwise
        bool eventPending(short events, struct timeval *tv);

        // Returns true if "ready" (i.e. matching ready_flags has been set)
        // events is any of EV_TIMEOUT|EV_READ|EV_WRITE|EV_SIGNAL
        // Returns true if ready on events, false otherwise
        bool eventReady(short events);

        short getFlags() {return(flags_);} // set on create
        virtual void setFlags(short flgs); // Always, don't set flags_ directly

        // ready-flags set on ready
        short getReadyFlags() {return(ready_flags_);}
        void setReadyFlags(short ready_flags) {ready_flags_ = ready_flags;}
        void orIntoReadyFlags(short ready_flags) {ready_flags_ |= ready_flags;}
        void resetReadyFlags() {ready_flags_ = 0;}

        uint64_t getUserDataUi64() const {return(user_data_);}
        Fd getUserData() const {return(((Fd)user_data_));}
        void setUserData(uint64_t user_data) { user_data_ = user_data; }

        virtual EmEventType getEmEventType() const { return(EmEvReg); }

        // checks emee_cptr_set_
        EventMethEpollEquivImpl * getEventMethEpollEquivImpl();

        void detachEventMethEpollEquiv();

        // made virtual since can't call destructor on non-final type with
        // virtual function(s) but non-virtual destructor
        virtual ~EmEvent();

        // To be called only from EventMethEpollEquiv::handleEventCallback
        virtual void handleEventCallback(// almost private
                                  [[maybe_unused]] short & ev_flags_in_out) {};

        // To be called only from EventMethEpollEquiv::ctl() - almost private
        void resetAddWasArtificial()
            {
                #ifdef DEBUG
                if (add_was_artificial_)
                    PS_LOG_DEBUG_ARGS("Reset add_was_artificial_ for Fd %p",
                                      this);
                #endif
                add_was_artificial_ = false;
            }

        // To be called only from EventMethEpollEquiv::ctlEx() and callbacks -
        // almost private
        bool addWasArtificial() { return(add_was_artificial_); }
        
                

    protected:
        EmEvent();

        // init is clled from make_new, or from a construction function of a
        // derived class
        EmEvent * init(int actual_fd,
                              short flags,
                              // For setfd and setfl arg:
                              //   F_SETFDL_NOTHING - change nothing
                              //   Zero or pos number that is not
                              //   F_SETFDL_NOTHING - set flags to value of
                              //   arg, and clear any other flags
                              //   Neg number that is not F_SETFDL_NOTHING
                              //   - set flags that are set in (0 - arg),
                              //   but don't clear any flags
                              int f_setfd_flags,   // e.g. FD_CLOEXEC
                              int f_setfl_flags);  // e.g. O_NONBLOCK

        void setPriorTv(const std::chrono::milliseconds * timeval_cptr);

    protected:
        struct event * ev_;
        short flags_; // Always use setFlags to set, don't set flags_ directly
                      // Are set on create, or, sometimes on mod/rearm

        bool add_was_artificial_; // This flag is set when the EmEvent has been
                                  // added to event_meth_epoll_equiv_impl_ but
                                  // by internal eventmeth code not by other
                                  // Pistache code calling
                                  // ctl(Add...). This can happen with
                                  // settime, for instance. If other pistache
                                  // code calls ctl(Add...) subsequently,
                                  // then add_was_artificial_ is reset.

        EventMethEpollEquivImpl * event_meth_epoll_equiv_impl_;
        
    private:
        short ready_flags_; // set when event becomes ready

        // user_data_ is provided by the caller; it is anaolg of epoll_data
        // which is provided to epoll_wait in epoll
        uint64_t user_data_;

        int requested_f_setfd_flags_;
        int requested_f_setfl_flags_;
        int requested_actual_fd_;

        struct timeval * prior_tv_cptr_;//either points to prior_tv_ or is null
        struct timeval prior_tv_; // for timeout

        static void setFdlFlagsHelper(int actual_fd,
                                      int get_cmd, // F_GETFD or F_GETFL
                                      int set_cmd, // F_SETFD or F_SETFL
                                      int f_setfdl_flags);
        void setFdlFlagsIfNeededAndActualFd(int actual_fd);

        // The main getActualFd() will throw rather than return an actual-fd
        // value for the derived class EmEventFd, whereas getActualFdPrv will
        // return -1 for that EmEventFd class. Typically code internal to
        // EmEvent should use getActualFdPrv(), whereas of course any code
        // external to EmEvent must use getActualFd (since getActualFdPrv is
        // private to EmEvent).
        #ifdef DEBUG
        public:// getActualFdPrv public so debug fn logPendingOrNot can call it
        #endif
        int getActualFdPrv() const;
        #ifdef DEBUG
        private:
        #endif
        // Make eventCallbackFn a friend so it can call getActualFdPrv
        friend void eventCallbackFn(em_socket_t, short, void *);
    };


    class EmEventCtr : public EmEvent // public so we can dynamic_cast
    {
    public:
        static Fd getAsFd(EmEventCtr * emefd)
            {return(static_cast<Fd>(emefd));}
        Fd getAsFd() {return(getAsFd(this));}

        // getActualFd must not be called for a EmEventFd
        int getActualFd() const override; // overriding EmEvent version

        // For read, write, poll rules, see definition of eventfd in Linux:
        //     e.g. https://www.man7.org/linux/man-pages/man2/eventfd.2.html

        ssize_t read(uint64_t * val_out_ptr);

        // buf must be at least 8 bytes long
        // read copies an 8-byte integer into buf
        ssize_t read(void * buf, size_t count) override;

        // Sets counter_val_ to zero. If counter_val_ already zero, does
        // nothing. Returns old counter_val.
        uint64_t resetCounterVal();

        int ctl(EvCtlAction op, //add,mod,del
                EventMethEpollEquivImpl * emee,
                short events,     // bitmask of EVM... events
                std::chrono::milliseconds * timeval_cptr) override;

        void setFlags(short flgs) override;// Always, don't set flags_ directly

        void makeBlocking();
        void makeNonBlocking();
        bool isBlocking();

        virtual EmEventType getEmEventType() const override {return(EmEvNone);}

    protected:
        ssize_t writeProt(const uint64_t val);

        // write copies an integer of length up to 8 from buf
        ssize_t writeProt(const void * buf, size_t count);

    public:
        // renewEv is public solely so it can be a friend of
        // EventMethEpollEquiv, allowing renewEv to call EventMethEpollEquiv's
        // private member function ctlEx(). renewEv itself should be called
        // solely from EmEventFd::read and EmEventFd::write.
        void renewEv(); // pseudo private

    protected:        
        EmEventCtr(uint64_t initval);
        
    private:
        std::mutex cv_read_mutex_;  // used in wait
        std::mutex cv_write_mutex_; // used in wait

        // cv_xxx_sptr_ are null if EmEventFd is nonblocking
        std::shared_ptr<std::condition_variable> cv_read_sptr_;
        std::shared_ptr<std::condition_variable> cv_write_sptr_;

        std::mutex counter_val_mutex_; // Also guards condition_variable
        uint64_t counter_val_;

        std::mutex block_nonblock_mutex_;

        // Sets counter_val_ to zero. If counter_val_ already zero, does
        // nothing. Returns old counter_val.
        // counter_val_mutex_ must be locked prior to calling
        uint64_t resetCounterValMutexAlreadyLocked();
    };

    // EmEventFd implements semantics similar to the Fd returned by eventfd in
    // Linux. It does _not_ have an associated OS fd.
    // EFD_SEMAPHORE is not implemented since Pistache doesn't seem to use it
    //
    // Regarding poll/epoll/select/libevent-loop:
    //   - EmEventFd is readable if the counter > 0
    //   - EmEventFd is writable if 1 can be written without blocking
    // (Note - we ignore the possibility of an overflow caused by 2^64 eventfd
    //  "signal posts" - won't happen)
    // 
    // Where appropriate, we can use libevent's event_active to make the event
    // active
    class EmEventFd final : public EmEventCtr // public so we can dynamic_cast
    {
    public:
        
        // Will return NULL if not an EmEventFd
        static FdEventFd getFromEmEventCPtr(Fd eme_cptr);
        static FdEventFd getFromEmEventCPtrNoLogIfNull(Fd eme_cptr);
        static FdEventFd getFromFd(Fd fd) {return(getFromEmEventCPtr(fd));}

        static EmEventFd * make_new(unsigned int initval,
                              // For setfd and setfl arg:
                              //   F_SETFDL_NOTHING - change nothing
                              //   Zero or pos number that is not
                              //   F_SETFDL_NOTHING - set flags to value of
                              //   arg, and clear any other flags
                              //   Neg number that is not F_SETFDL_NOTHING
                              //   - set flags that are set in (0 - arg),
                              //   but don't clear any flags
                              int f_setfd_flags,   // e.g. FD_CLOEXEC
                              int f_setfl_flags);  // e.g. O_NONBLOCK

        ssize_t write(const uint64_t val) {return(writeProt(val));}

        // write copies an integer of length up to 8 from buf
        ssize_t write(const void * buf, size_t count) override
                                               {return(writeProt(buf, count));}

        EmEventType getEmEventType() const override { return(EmEvEventFd); }

        
        
    private:
        EmEventFd(uint64_t initval);
    };

    // EmEventTmrFd implements semantics similar to the Fd returned by
    // timerfd_create in Linux. It does _not_ have an associated OS fd.
    //
    // The EmEventTmrFd count holds the number of times the timer has expired
    // since the most recent call to read or settime, either of which sets the
    // count to zero. Read returns the value of count prior to the reset.
    // 
    // If read is attempted while count is already zero, the thread will block
    // if the EmEventTmrFd is blocking; or, if the EmEventTmrFd is nonblocking,
    // -1 is returned and errno set to EAGAIN.
    //
    // Regarding poll/epoll/select/libevent-loop:
    //   - EmEventTmrFd is readable if the counter > 0
    //   - EmEventTmrFd is writable if 1 can be written without blocking
    // (Note - we ignore the possibility of an overflow caused by 2^64 eventfd
    //  "signal posts" - won't happen)
    // 
    // Where appropriate, we can use libevent's event_active to make the event
    // active
    class EmEventTmrFd final : public EmEventCtr//public so we can dynamic_cast
    {
    public:
        
        // Will return NULL if not an EmEventTmrFd
        static FdEventTmrFd getFromEmEventCPtr(Fd eme_cptr);
        static FdEventTmrFd getFromEmEventCPtrNoLogIfNull(Fd eme_cptr);
        static FdEventTmrFd getFromFd(Fd fd) {return(getFromEmEventCPtr(fd));}

        // macOS claims to support a number of clocks - see "man clock_gettime"
        // for the list. In practice, pistache seems to use only
        // CLOCK_MONOTONIC. For now we accept any of the monotonic system-wide
        // clocks, and reject any others - in particular, we reject
        // CLOCK_REALTIME (which can change value) as well as the thread and
        // process time clocks.
        // Consistent with not supporting CLOCK_REALTIME, we do not support
        // TFD_TIMER_CANCEL_ON_SET nor do we have to support discontinuous
        // changes in the clock value
        // If emee is NULL here, it will need to be supplied when settime is
        // called
        static EmEventTmrFd * make_new(clockid_t clock_id,
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
                              EventMethEpollEquivImpl * emee/*may be NULL*/);
        

        // settime is analagous to timerfd_settime in linux
        // 
        // The linux flags TFD_TIMER_ABSTIME and TFD_TIMER_CANCEL_ON_SET are
        // not supported
        //
        // Since pistache doesn't use the "struct itimerspec * old_value"
        // feature of timerfd_settime, we haven't implemented that feature.
        // 
        // If new_timeval_cptr is NULL or *new_timeval_cptr is all zero, the
        // settime call will reset the timer (again as per timerfd_settime)
        // 
        // Note: settime is in EmEvent rather than solely in EmEventTmrFd since
        // any kind of event may have a timeout set, not only timer events
        int settime(const std::chrono::milliseconds* new_timeval_cptr,
                EventMethEpollEquivImpl * emee = NULL/*may be NULL*/) override;


        EmEventType getEmEventType() const override { return(EmEvTimer); }

    protected:
        void handleEventCallback(short & ev_flags_in_out) override;

    private:
        // NO ssize_t write(const uint64_t val);
        ssize_t write(const void * buf, size_t count) override;// always fails

    private:
        EmEventTmrFd(clockid_t clock_id,
                     EventMethEpollEquivImpl * emee/*may be NULL*/);
    };

} // namespace Pistache

#ifdef DEBUG
namespace Pistache
{
    static std::set<const EmEvent *> dbg_emv_set;
    static std::mutex dbg_emv_set_mutex;

    static void dbg_new_emv(const EmEvent * emv)
    {
        std::lock_guard<std::mutex> l_guard(dbg_emv_set_mutex);
        dbg_emv_set.insert(emv);
    }

    static void dbg_delete_emv(const EmEvent * emv)
    {
        std::lock_guard<std::mutex> l_guard(dbg_emv_set_mutex);
        dbg_emv_set.erase(emv);
    }

    void dbg_log_all_emes()
    {
        std::lock_guard<std::mutex> l_guard(dbg_emv_set_mutex);
        PS_LOG_DEBUG_ARGS("Full set of %u EmEvent * follows:",
                          dbg_emv_set.size());
    
        for(auto it = dbg_emv_set.begin(); it != dbg_emv_set.end(); it++)
        {
            bool break_out = false;
            std::stringstream sss;
            sss << "    EmEvents: ";
        
            for(unsigned int i = 0; i<6; i++)
            {
                if (i != 0)
                {
                    it++;
                    if (it == dbg_emv_set.end())
                    {
                        break_out = true;
                        break;
                    }
                    sss << " ";
                }

                const EmEvent * eme = *it;
                sss << eme;
            }
        
            const std::string s = sss.str();
            PS_LOG_DEBUG_ARGS("%s", s.c_str());

            if (break_out)
                break;
        }
    }
}


#define DBG_NEW_EMV(__EME) dbg_new_emv(__EME)
#define DBG_DELETE_EMV(__EME) dbg_delete_emv(__EME)

#else // ifdef DEBUG
// Not DEBUG
#define DBG_NEW_EMV(__EME)
#define DBG_DELETE_EMV(__EME)
#endif

/* ------------------------------------------------------------------------- */

#include <sys/errno.h>

#include <chrono>
#include <set>

#include <pistache/pist_check.h>
#include <pistache/pist_timelog.h>
#include <pistache/os.h>

#include <unistd.h> // for close
#include <fcntl.h>  // for fcntl
#include <assert.h>

#ifdef DEBUG
#include <libgen.h> // for basename_r
#include <sys/param.h> // for MAXPATHLEN
#include <atomic> // for std::atomic_int
#endif

#include <signal.h> // for signal constants e.g. SIGABRT or SIGURG

#include <event2/event.h>

/* ------------------------------------------------------------------------- */
//
// EventMethEpollEquiv methods - mostly just pass calls to
// EventMethEpollEquivImpl
//

namespace Pistache
{
    // size is a hint as to how many FDs to be monitored
    std::shared_ptr<EventMethEpollEquiv> EventMethFns::create(int size)
    { // size is a hint as to how many FDs to be monitored
        PS_TIMEDBG_START;

        EventMethEpollEquiv * emee_cptr = new EventMethEpollEquiv(size);
        return(std::shared_ptr<EventMethEpollEquiv>(emee_cptr));
    }

    
    #define F_SETFDL_NOTHING ((int)((unsigned) 0x8A82))
    Fd EventMethFns::em_event_new(
                        em_socket_t actual_fd,//file desc, signal, or -1
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
            )
    {
        return(EventMethEpollEquivImpl::em_event_new(actual_fd, flags,
                                                f_setfd_flags, f_setfl_flags));
    }
    

    // If emee is NULL here, it will need to be supplied when settime is
    // called
    Fd EventMethFns::em_timer_new(clockid_t clock_id,
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
                               EventMethEpollEquiv * emee/*may be NULL*/)
    {
        return(EventMethEpollEquivImpl::em_timer_new(clock_id,
                                                  f_setfd_flags, f_setfl_flags,
                                             getEMEEImpl(emee)));
    }
    

    // For "eventfd-style" descriptors
    // Note that FdEventFd does not have an "actual fd" that the caller can
    // access; the caller must use FdEventFd's member functions instead
    FdEventFd EventMethFns::em_eventfd_new(unsigned int initval,
                                 int f_setfd_flags, // e.g. FD_CLOEXEC
                                 int f_setfl_flags) // e.g. O_NONBLOCK
    {
        return(EventMethEpollEquivImpl::em_eventfd_new(initval,
                                                f_setfd_flags, f_setfl_flags));
    }



    // Returns 0 for success, on error -1 with errno set
    // Will add/remove from interest_ if appropriate
    int EventMethFns::ctl(EvCtlAction op, // add, mod, or del
                       EventMethEpollEquiv * epoll_equiv,
                       Fd event, // libevent event
                       short events, // bitmask per epoll_ctl (EVM_... events)
                       std::chrono::milliseconds * timeval_cptr)
    {
        return(EventMethEpollEquivImpl::ctl(op, getEMEEImpl(epoll_equiv),
                                            event, events, timeval_cptr));
    }
    

    // rets 0 on success, -1 error
    int EventMethFns::closeEvent(EmEvent * em_event)
    {
        return(EventMethEpollEquivImpl::closeEvent(em_event));
    }
    // See also CLOSE_FD macro

    int EventMethFns::getActualFd(const EmEvent * em_event)
    {
        return(EventMethEpollEquivImpl::getActualFd(em_event));
    }

    // efd should be a pointer to EmEventFd - does dynamic cast
    ssize_t EventMethFns::writeEfd(EmEvent * efd, const uint64_t val)
    {
        return(EventMethEpollEquivImpl::writeEfd(efd, val));
    }
    
    ssize_t EventMethFns::readEfd(EmEvent * efd, uint64_t * val_out_ptr)
    {
        return(EventMethEpollEquivImpl::readEfd(efd, val_out_ptr));
    }
    

    ssize_t EventMethFns::read(EmEvent * fd, void * buf, size_t count)
    {
        return(EventMethEpollEquivImpl::read(fd, buf, count));
    }
    
    ssize_t EventMethFns::write(EmEvent * fd,
                                       const void * buf, size_t count)
    {
        return(EventMethEpollEquivImpl::write(fd, buf, count));
    }
    

    EmEvent * EventMethFns::getAsEmEvent(EmEventFd * efd)
    {
        return(EventMethEpollEquivImpl::getAsEmEvent(efd));
    }
    

    uint64_t EventMethFns::getEmEventUserDataUi64(const EmEvent * fd)
    {
        return(EventMethEpollEquivImpl::getEmEventUserDataUi64(fd));
    }
    
    Fd EventMethFns::getEmEventUserData(const EmEvent * fd)
    {
        return(EventMethEpollEquivImpl::getEmEventUserData(fd));
    }
    
    void EventMethFns::setEmEventUserData(EmEvent * fd,
                                                 uint64_t user_data)
    {
        EventMethEpollEquivImpl::setEmEventUserData(fd, user_data);
    }
    
    void EventMethFns::setEmEventUserData(EmEvent * fd, Fd user_data)
    {
        EventMethEpollEquivImpl::setEmEventUserData(fd, user_data);
    }
    

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
    int EventMethFns::setEmEventTime(EmEvent * fd,
                            const std::chrono::milliseconds * new_timeval_cptr,
                            EventMethEpollEquiv * emee/*may be NULL*/)
    {
        return(EventMethEpollEquivImpl::setEmEventTime(fd, new_timeval_cptr,
                                             getEMEEImpl(emee)));
    }
    

    EmEventType EventMethFns::getEmEventType(EmEvent * fd)
    {
        return(EventMethEpollEquivImpl::getEmEventType(fd));
    }

    void EventMethFns::resetEmEventReadyFlags(EmEvent * fd)
    {
        EventMethEpollEquivImpl::resetEmEventReadyFlags(fd);
    }

    EventMethEpollEquivImpl * EventMethFns::getEMEEImpl(
                                     EventMethEpollEquiv * emee/*may be NULL*/)
    {
        return(emee ? emee->impl_.get() : NULL);
    }
    
    #ifdef DEBUG
    std::string EventMethFns::getActFdAndFdlFlagsAsStr(int actual_fd)
    {
        return(EventMethEpollEquivImpl::getActFdAndFdlFlagsAsStr(actual_fd));
    }
    #endif

    #ifdef DEBUG
    int EventMethFns::getEmEventCount()
    {
        return(EventMethEpollEquivImpl::getEmEventCount());
    }
    
    int EventMethFns::getLibeventEventCount()
    {
        return(EventMethEpollEquivImpl::getLibeventEventCount());
    }
    
    int EventMethFns::getEventMethEpollEquivCount()
    {
        return(EventMethEpollEquivImpl::getEventMethEpollEquivCount());
    }
    
    int EventMethFns::getEventMethBaseCount()
    {
        return(EventMethEpollEquivImpl::getEventMethBaseCount());
    }
    
    int EventMethFns::getWaitThenGetAndEmptyReadyEvsCount()
    {
        return(EventMethEpollEquivImpl::getWaitThenGetAndEmptyReadyEvsCount());
    }
    #endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
// EventMethEpollEquiv methods - mostly just pass calls to
// EventMethEpollEquivImpl
//

    EventMethEpollEquiv::EventMethEpollEquiv(int size)
    {
        impl_ = std::make_unique<EventMethEpollEquivImpl>(size);
        if (!impl_)
        {
            PS_LOG_WARNING("impl_ NULL");
            throw std::runtime_error("impl_ NULL");
        }
    }

    // Add to interest list
    // Returns 0 for success, on error -1 with errno set
    int EventMethEpollEquiv::ctl(EvCtlAction op, // add, mod, or del
                                 Fd em_event,
                                 short     events, // bitmask of EVM_... events
                                 std::chrono::milliseconds * timeval_cptr)
    {
        return(impl_->ctl(op, em_event, events, timeval_cptr));
    }

    // unlockInterestMutexIfLocked is used only in conjunction with
    // getReadyEmEvents
    void EventMethEpollEquiv::unlockInterestMutexIfLocked()
    {
        impl_->unlockInterestMutexIfLocked();
    }

    // Waits (if needed) until events are ready, then sets the _out set to
    // be equal to ready events, and empties the list of ready events
    // "timeout" is in milliseconds, or -1 means wait indefinitely
    // Returns number of ready events being returned; or 0 if timed-out
    // without an event becoming ready; or -1, with errno set, on error
    int EventMethEpollEquiv::getReadyEmEvents(int timeout,
                                           std::set<Fd> & ready_evm_events_out)
    {
        return(impl_->getReadyEmEvents(timeout, ready_evm_events_out));
    }

    // EvEvents are some combination of EVM_TIMEOUT, EVM_READ, EVM_WRITE,
    // EVM_SIGNAL, EVM_PERSIST, EVM_ET, EVM_CLOSED

    int EventMethEpollEquiv::toEvEvents(
                                      const Flags<Polling::NotifyOn>& interest)
    {
        return(impl_->toEvEvents(interest));
    }
    

    Flags<Polling::NotifyOn> EventMethEpollEquiv::toNotifyOn(Fd fd) 
    { // uses fd->ready_flags
        return(impl_->toNotifyOn(fd));
    }

    EventMethEpollEquiv::~EventMethEpollEquiv()
    {
        PS_TIMEDBG_START_THIS;

        impl_ = NULL;
    }

    
} // namespace Pistache

/* ------------------------------------------------------------------------- */

#ifdef DEBUG
static std::atomic_int em_event_count__ = 0; // maintained by EmEvent
static std::atomic_int libevent_event_count__ = 0; // maintained by EmEvent
static std::atomic_int event_meth_epoll_equiv_count__ = 0;//EventMethEpollEquiv
static std::atomic_int event_meth_base_count__ = 0; // by EventMethBase
static std::atomic_int wait_then_get_count__ = 0; // by EventMethEpollEquiv

#define INC_DEBUG_CTR(_DCTR_NAME_) _DCTR_NAME_##_count__++
#define DEC_DEBUG_CTR(_DCTR_NAME_) _DCTR_NAME_##_count__--
#else
#define INC_DEBUG_CTR(_DCTR_NAME_)
#define DEC_DEBUG_CTR(_DCTR_NAME_)
#endif

/* ------------------------------------------------------------------------- */

    #ifdef DEBUG
    
    #define NAME_EVM_FLAG(CAPS_NAME, TITLE_CASE_NAME)                   \
        if (flags & EVM_##CAPS_NAME)                                    \
        {                                                               \
            if (fst_flag)                                               \
                fst_flag = false;                                       \
            else                                                        \
                (*res) += " ";                                          \
                                                                        \
            (*res) += PIST_QUOTE(TITLE_CASE_NAME);                      \
        }

    static std::shared_ptr<std::string> evmFlagsToStdString(short flags)
    {
        std::shared_ptr<std::string> res(
            std::make_shared<std::string>("0x"));

        std::stringstream ss;
        ss << std::hex << flags;
        const std::string s = ss.str();
        (*res) += s;

        if (flags)
        {
            (*res) += " ";
            bool fst_flag = true;
            
            NAME_EVM_FLAG(TIMEOUT, Timeout);
            NAME_EVM_FLAG(READ, Read);
            NAME_EVM_FLAG(WRITE, Write);
            NAME_EVM_FLAG(SIGNAL, Signal);
            NAME_EVM_FLAG(PERSIST, Persist);
            NAME_EVM_FLAG(ET, Edge);
            NAME_EVM_FLAG(CLOSED, Closed);

            if (flags > (EVM_TIMEOUT + EVM_READ + EVM_WRITE + EVM_SIGNAL +
                         EVM_PERSIST + EVM_ET + EVM_CLOSED))
            {
                if (!fst_flag)
                    (*res) += ", and ";
                (*res) += "unknown value(s)";
                fst_flag = false;
            }
        }

        return(res);
    }
    #endif // of ifdef DEBUG

/* ------------------------------------------------------------------------- */


extern "C" void eventCallbackFn(em_socket_t cb_actual_fd,
                                short ev_flags, // One or more EV_* flags
                                void * cb_arg) // caller-supplied arg
{
    PS_TIMEDBG_START_SQUARE;

    PS_LOG_DEBUG_ARGS("callback actual-fd %d, ev_flags %s, EmEvent %p",
                      cb_actual_fd,
                      evmFlagsToStdString(ev_flags)->c_str(),
                      cb_arg);

    // "arg" will be a pointer to an EmEvent, all being well
    if (!cb_arg)
    {
        PS_LOG_WARNING("arg null");
        return;
    }
    
    if (cb_arg == ((void *) -1))
    {
        PS_LOG_WARNING("arg -1");
        return;
    }

    // Note: Do not dereference the EmEvent (cb_arg) here. It may be deleted in
    // another thread at any time. You can't use it safely until additional
    // mutex(es) are claimed in epoll_equiv->handleEventCallback(...).

    Pistache::EventMethEpollEquivImpl * epoll_equiv = NULL;
    if (!Pistache::EventMethEpollEquivImpl::findEmEventInAnInterestSet(
                                                         cb_arg, &epoll_equiv))
    {
        #ifdef DEBUG
        PS_LOG_INFO_ARGS("EmEvent as arg %p not found", cb_arg);
        #endif
        return;
    }

    if (!epoll_equiv)
    {
        PS_LOG_WARNING("epoll_equiv is null for EmEvent as arg %p, arg");
        return;
    }

    epoll_equiv->handleEventCallback(cb_arg, cb_actual_fd, ev_flags);
}

// Why and how we use libevent's finalize
//
// Whenever we do a libevent event_new, we set the EV_FINALIZE flag
//
// Otherwise libevent will have a de facto mutex on the libevent event such
// that the ev_ mutex is claimed before a callback and before allowing
// event_del to execute. So, when an event is being closed, interest_mutex_ is
// claimed (in eventmeth), and then event_del is called, claiming the ev_
// mutex. When a callback happens, libevent claims the ev_ mutex, and then our
// code claims the interest_mutex_. I.e. the two mutexes (interest_mutex_ and
// ev_ mutex) are claimed in the opposite order for a closeEvent vs. a libevent
// callback, causing a deadlock on a race condition.
//
// Subsequently, where we would have otherwise have done event_del and
// event_free, we instead do event_free_finalize. This makes libevent do the
// event_del+event_free for us, but out of the event loop thread so that they
// won't happen while an event callback handler has been called (instead, the
// del and free happen after the handler has run to completion).
//
// However, when we are doing an event_del but not event_free, we don't use
// event_finalize. You can't do event_add again after an event_finalize, which
// may want to do if we're not doing event_free.

// Use for event_free_finalize
extern "C" void libevEventFinalizeAndFreeCallback(
            [[maybe_unused]] struct event * ev, [[maybe_unused]] void * cb_arg)
{
    PS_TIMEDBG_START;

    PS_LOG_DEBUG_ARGS("Finalize+free cb for ev %p of EmEvent %p",
                      ev, cb_arg);

    // Note - no need to call event_del, it is a no-op here; libevent takes
    // care of it for us. Libevent also does event_free as soon as this
    // callback function returns (presuming event_free_finalize was used to
    // invoke this callback).

    DEC_DEBUG_CTR(libevent_event);
}


/* ------------------------------------------------------------------------- */


namespace Pistache
{
    class EventMethBase
    { // Note there is one EventMethBase per each EventMethEpollEquiv
    public:
        EventMethBase();
        ~EventMethBase();
        
        struct event_base * getEventBase() {return(event_base_);}
        static int getEventBaseFeatures() {return(event_base_features_);}

        // Calls event_base_loopbreak for this base
        int eMBaseLoopbreak(); // Calls event_base_loopbreak for this base

    private:
        struct event_base * event_base_;
        
        static bool event_meth_base_inited_previously;
        static std::mutex event_meth_base_inited_previously_mutex;

        static int event_base_features_;
    };

    // Note: A new event_base is created for each EventMethBase (i.e. they are
    // not global). However, we only need to read and save event_base_features_
    // once, so that we do make global
    int EventMethBase::event_base_features_ = 0;
    bool EventMethBase::event_meth_base_inited_previously = false;
    std::mutex EventMethBase::event_meth_base_inited_previously_mutex;

    EventMethBase::EventMethBase() :
        event_base_(NULL)
    {
        PS_TIMEDBG_START;

        if (event_meth_base_inited_previously)
        {
            event_base_ = TRY_NULL_RET(event_base_new());
            INC_DEBUG_CTR(event_meth_base);
            return;
        }

        GUARD_AND_DBG_LOG(event_meth_base_inited_previously_mutex);

        if (event_meth_base_inited_previously)
        {
            event_base_ = TRY_NULL_RET(event_base_new());
            INC_DEBUG_CTR(event_meth_base);
            return;
        }
        
        event_meth_base_inited_previously = true;
        #ifdef _WIN32 // Defined for both 32-bit and 64-bit environments
        evthread_use_windows_threads();
        #else
        evthread_use_pthreads();
        #endif

        event_base_ = TRY_NULL_RET(event_base_new());

        event_base_features_ = event_base_get_features(event_base_);
        if (!(event_base_features_ & EV_FEATURE_ET))
        {
            PS_LOG_WARNING("No edge trigger");
            throw std::system_error(EOPNOTSUPP, std::generic_category(),
                                    "No edge trigger");
            // Because EV_ET is used, e.g. see Epoll::addFd
        }

        INC_DEBUG_CTR(event_meth_base);
    }

    EventMethBase::~EventMethBase()
    {
        DEC_DEBUG_CTR(event_meth_base);

        PS_TIMEDBG_START_THIS;

        if (event_base_)
        {
            event_base_free(event_base_);
            event_base_ = 0;
        }
    }
    

/* ------------------------------------------------------------------------- */

    int EmEventCtr::getActualFd() const // overridden from EmEvent
    {
        PS_LOG_WARNING_ARGS("EmEventCtr (EmEvent) %p has no actual-fd", this);
        PS_LOGDBG_STACK_TRACE;

        throw std::runtime_error("No actual-fd allowed for EmEventCtr");
    }

    EmEventCtr::EmEventCtr(uint64_t initval) :
        counter_val_(initval)
    {
    }

    // Sets counter_val_ to zero. If counter_val_ already zero, does
    // nothing. Returns old counter_val.
    // counter_val_mutex_ must NOT be locked prior to calling
    uint64_t EmEventCtr::resetCounterVal()
    {
        GUARD_AND_DBG_LOG(counter_val_mutex_);
        return(resetCounterValMutexAlreadyLocked());
    }
    
        
    // Sets counter_val_ to zero. If counter_val_ already zero, does
    // nothing. Returns old counter_val.
    // counter_val_mutex_ must be locked prior to calling
    uint64_t EmEventCtr::resetCounterValMutexAlreadyLocked()
    {
        uint64_t old_counter_val = counter_val_;
        
        if (counter_val_ != 0)
        {
            counter_val_ = 0;
            PS_LOG_DEBUG_ARGS("EmEventCtr %p zeroed counter, old value %u",
                              this, old_counter_val);

            if (ev_)
            { 
                if (flags_ & EVM_READ)
                { // counter_val_ was readable, but no longer is
                    renewEv();
                }
                else if ((getEmEventType() != EmEvTimer) &&
                         (flags_ & EVM_WRITE) &&
                         (old_counter_val >= 0xfffffffffffffffe))
                { // counter_val_ was not writable, but has become so now
                    PS_LOG_DEBUG_ARGS(
                        "EmEventCtr %p being activated for write", this);
                    event_active(ev_, EV_WRITE, 0 /* obsolete parm*/);
                }
            }

            std::shared_ptr<std::condition_variable> tmp_cv_sptr(
                /* Use tmp variable in case set to zero */ cv_write_sptr_);
            if (tmp_cv_sptr)
            {
                PS_LOG_DEBUG_ARGS(
                    "EmEventCtr %p waking up any blocked writes", this);

                { // encapsulate cv_write_mutex_ lock
                    // 
                    // Per spec, must claim and release the mutex before
                    // doing a notify_all
                    // https://en.cppreference.com/w/cpp/thread/
                    //                                condition_variable/wait
                    // (See example)
                    GUARD_AND_DBG_LOG(cv_write_mutex_);
                }
                    
                tmp_cv_sptr->notify_all(); // does nothing if none waiting
            }
        }

        return(old_counter_val);
    }
    
    ssize_t EmEventCtr::read(uint64_t * val_out_ptr)
    {
        PS_TIMEDBG_START_ARGS("Read EmEventCtr %p", this);

        if (!val_out_ptr)
        {
            PS_LOG_DEBUG("val_out_ptr null");
            errno = EINVAL;
            return(-1);
        }

        { // encapsulate counter_val_mutex_
            GUARD_AND_DBG_LOG(counter_val_mutex_);
            
            uint64_t old_counter_val = resetCounterValMutexAlreadyLocked();
            if (old_counter_val)
            {
                if (val_out_ptr)
                    *val_out_ptr = old_counter_val;

                return(sizeof(counter_val_));
            }

            // counter_val_ is zero, so not readable

            if (!cv_read_sptr_)
            {
                errno = EAGAIN;
                return(-1);
            }

            PS_LOG_DEBUG_ARGS(
                "EmEventCtr %p blocking until counter nonzero", this);

            std::unique_lock<std::mutex> lk(cv_read_mutex_);
            cv_read_sptr_->wait(lk);
        }
        
        PS_LOG_DEBUG_ARGS("EmEventCtr %p unblocked after read", this);
        return(this->read(val_out_ptr));
    }

    ssize_t EmEventCtr::writeProt(const uint64_t val)
    {
        PS_TIMEDBG_START_ARGS("Write EmEventCtr %p with val %u", this, val);

        if (val == 0)
            return(sizeof(val)); // nothing to do

        if (val == 0xffffffffffffffff)
        {
            errno = EINVAL;
            return(-1);
        }

        { // encapsulate counter_val_mutex_
            GUARD_AND_DBG_LOG(counter_val_mutex_);

            uint64_t max_writable_val = (0xfffffffffffffffe - counter_val_);
            if (val > max_writable_val)
            {
                if (!cv_write_sptr_)
                {
                    errno = EAGAIN;
                    return(-1);
                }

                PS_LOG_DEBUG_ARGS(
                    "EmEventCtr %p blocking until counter read", this);

                        std::unique_lock<std::mutex> lk(cv_write_mutex_);
                        cv_write_sptr_->wait(lk);
            }
            else
            {
                uint64_t old_counter_val = counter_val_;
                counter_val_ += val;
                PS_LOG_DEBUG_ARGS("EmEventCtr %p wrote %u, new counter %u",
                                  this, val, counter_val_);

                if (ev_)
                {
                    if ((getEmEventType() != EmEvTimer) &&
                        (old_counter_val < 0xfffffffffffffffe) &&
                        (flags_ & EVM_WRITE) &&
                        (counter_val_ >= 0xfffffffffffffffe))
                    { // counter_val_ was writable, but no longer is
                        renewEv();
                    }
                    else if ((flags_ & EVM_READ) && (!old_counter_val))
                    { // counter_val_ was not readable before, but now is
                        PS_LOG_DEBUG_ARGS("EmEventCtr %p activating for read",
                                          this);

                        short flags = EV_READ;
                    
                        if ((getEmEventType() != EmEvTimer) &&
                            (flags_ & EVM_WRITE) &&
                            (counter_val_ < 0xfffffffffffffffe))
                        { // it's also writable
                            PS_LOG_DEBUG_ARGS(
                                "EmEventCtr %p also being activated for write",
                                        this);
                        
                            flags |= EV_WRITE;
                        }

                        event_active(ev_, flags, 0 /* obsolete parm*/);
                    }
                }
                

                std::shared_ptr<std::condition_variable> tmp_cv_sptr(
                    /* Use tmp variable in case set to zero */ cv_read_sptr_);
                if (tmp_cv_sptr)
                {
                    PS_LOG_DEBUG_ARGS(
                        "EmEventCtr %p waking up any blocked reads", this);

                    { // encapsulate cv_read_mutex_) lock
                      // 
                      // Per spec, must claim and release the mutex before
                      // doing a notify_all
                      // https://en.cppreference.com/w/cpp/thread/
                      //                                condition_variable/wait
                      // (See example)
                        GUARD_AND_DBG_LOG(cv_read_mutex_);
                    }
                    
                    tmp_cv_sptr->notify_all();// does nothing if none waiting
                }

                return(sizeof(val));
            }
        }

        PS_LOG_DEBUG_ARGS("EmEventCtr %p unblocked", this);
        return(this->writeProt(val));
    }

    // buf must be at least 8 bytes long
    // read copies an 8-byte integer into buf
    ssize_t EmEventCtr::read(void * buf, size_t count)
    {
        if (!buf)
        {
            errno = EINVAL;
            PS_LOG_INFO("buf null");
            return(-1);
        }

        if (count < 8)
        {
            errno = EINVAL;
            PS_LOG_INFO("count too small");
            return(-1);
        }
        

        if (count > 8)
        {
            PS_LOG_DEBUG_ARGS("EmEventCtr::read count is not 8 but %u", count);
            memset(buf, 0, count);
        }

        return(this->read((uint64_t *)buf));
    }

    // write copies an integer of length up to 8 from buf
    ssize_t EmEventCtr::writeProt(const void * buf, size_t count)
    {
        if (!buf)
        {
            errno = EINVAL;
            PS_LOG_INFO("buf null");
            return(-1);
        }

        if (count == 8)
            return(this->writeProt(*((uint64_t *)buf)));

        PS_LOG_DEBUG_ARGS("EmEventCtr::write count is not 8 but %u", count);
        
        uint64_t val = 0;
        memcpy(&val, buf, std::min(count, sizeof(val)));
        return(this->writeProt(val));
    }

    void EmEventCtr::makeBlocking()
    {
        PS_TIMEDBG_START_THIS;

        GUARD_AND_DBG_LOG(block_nonblock_mutex_);
        
        if (cv_read_sptr_)
        {
            PS_LOG_DEBUG_ARGS("EmEventCtr %p already blocking", this);
            return; // already blocking, nothing to do
        }
        
        cv_read_sptr_  = std::make_shared<std::condition_variable>();
        if (getEmEventType() != EmEvTimer) // Timer not writable
            cv_write_sptr_ = std::make_shared<std::condition_variable>();
    }
    
    void EmEventCtr::makeNonBlocking()
    {
        PS_TIMEDBG_START_ARGS("EmEventCtr %p", this);
        GUARD_AND_DBG_LOG(block_nonblock_mutex_);

        if (!cv_read_sptr_)
        {
            PS_LOG_DEBUG_ARGS("EmEventCtr %p already nonblocking", this);
            return; // already nonblocking, nothing to do
        }
        
        cv_read_sptr_ = NULL;
        cv_write_sptr_ = NULL;
    }

    bool EmEventCtr::isBlocking()
    {
        GUARD_AND_DBG_LOG(block_nonblock_mutex_);
        return(cv_read_sptr_.get());
    }

    int EmEventCtr::ctl(EvCtlAction op, // add,mod,del
                        EventMethEpollEquivImpl * emee,
                        short events,     // bitmask of EVM... events
                        std::chrono::milliseconds * timeval_cptr) // override
    {
        PS_TIMEDBG_START_ARGS("EmEventCtr %p", this);
        
        struct event * old_ev = ev_;
        short old_flags = flags_;

        int res = EmEvent::ctl(op, emee, events, timeval_cptr);
        if (res != 0)
            return(res); // not success

        if (ev_)
        {
            GUARD_AND_DBG_LOG(counter_val_mutex_);

            int evfd_flags = 0; // flags that should be active
            int chgd_evfd_flags = 0; // Should-be-active flags that were
                                     // changed by EmEvent::ctl
            
            if ((flags_ & EVM_READ) && (counter_val_ > 0))
            {
                evfd_flags |= EV_READ;
                
                if ((!old_ev) || (!(old_flags & EVM_READ)))
                    chgd_evfd_flags |= EV_READ;
            }

            if ((getEmEventType() != EmEvTimer) &&
                (flags_ & EVM_WRITE) && (counter_val_ < 0xfffffffffffffffe))
            {
                evfd_flags |= EV_WRITE;
                
                if ((!old_ev) || (!(old_flags & EVM_WRITE)))
                    chgd_evfd_flags |= EV_WRITE;
            }

            if (chgd_evfd_flags) // activate libevent event
            {
                PS_LOG_DEBUG_ARGS("EmEventCtr %p being activated", this);
                event_active(ev_, evfd_flags, 0 /* obsolete parm*/);
            }
        }

        return(res);
    }

    // Always use, don't set flags_ directly
    void EmEventCtr::setFlags(short flgs) // override
    {
        EmEvent::setFlags(flgs);
    }

    void EmEventCtr::renewEv()
    {
        PS_TIMEDBG_START_ARGS("EmEventCtr %p", this);
        
        short old_flags = flags_;
        EventMethEpollEquivImpl * emee = getEventMethEpollEquivImpl();

        bool ev_in_emee = false;
        if (ev_)
        {
            if (emee)
            {
                ev_in_emee = (emee->findFdInInterest(this) != NULL);
                if (ev_in_emee)
                {
                    
                    int ctl_res = emee->ctlEx(EvCtlAction::Del,
                                              this,
                                              0 /* events*/,
                                              NULL /*timeval_cptr*/,
                                              true/*forceEmEventCtlOnly*/);
                    if (ctl_res != 0)
                    {
                        PS_LOG_INFO_ARGS(
                            "EmEventCtr %p failed to EvCtlAction::Del ev_ %p",
                            this, ev_);
                        throw std::runtime_error("EvCtlAction::Del failed");
                    }
                }
            }
            
            if (ev_)
            {
                event_free(ev_);
                ev_ = NULL;

                DEC_DEBUG_CTR(libevent_event);
            }
        }

        if (emee)
        {
            if (ev_in_emee)
            { // Have to add back the (new) ev_
                int ctl_res = emee->ctlEx(EvCtlAction::Add,
                           this,
                           old_flags /* events*/,
                           NULL /* timeval_cptr - use prior_tv_ if available*/,
                           true/*forceEmEventCtlOnly*/);
                if (ctl_res != 0)
                {
                    PS_LOG_INFO_ARGS(
                        "EmEventCtr %p failed to EvCtlAction::Add", this);
                    throw std::runtime_error("EvCtlAction::Add failed");
                }

                if (!ev_)
                { // ctl EvCtlAction::Add should cause ev_ to be created
                    PS_LOG_INFO_ARGS("EmEventCtr %p null ev_", this);
                    throw std::runtime_error("ev_ null");
                }
            }
        }
        
        short emefd_flags = 0;
        if ((flags_ & EVM_READ) && (counter_val_ > 0))
        {
            PS_LOG_DEBUG_ARGS("EmEventCtr %p renewal activating read",
                              this);
            emefd_flags |= EV_READ;
        }

        if ((getEmEventType() != EmEvTimer) && // timers aren't writable
            ((flags_ & EVM_WRITE) && (counter_val_ < 0xfffffffffffffffe)))
        {
            PS_LOG_DEBUG_ARGS("EmEventFd %p renewal activating write",
                              this);
            emefd_flags |= EV_WRITE;
        }

        if (emefd_flags)
        {
            if (!ev_)
            {
                PS_LOG_WARNING_ARGS("EmEventCtr %p can't activate with no ev_",
                                    this);
                throw std::runtime_error("ev_ null");
            }

            PS_LOG_DEBUG_ARGS("EmEventCtr %p activating in renewal", this);
            event_active(ev_, emefd_flags, 0 /* obsolete parm*/);
        }
    }

/* ------------------------------------------------------------------------- */

    #ifdef DEBUG
    static std::string fdlFlagsToStr(int fdl_flags)
    {
        if (fdl_flags == F_SETFDL_NOTHING)
            return(std::string("set nothing"));

        std::string res("set 0x");

        std::stringstream ss;
        ss << std::hex << ((fdl_flags >= 0) ? fdl_flags : (0 - fdl_flags));
        const std::string s = ss.str();
        res += s;

        res += ((fdl_flags >= 0) ? " clear any other" : " leave others");

        return(res);
    }

    std::string EventMethEpollEquivImpl::getActFdAndFdlFlagsAsStr(
                                                                 int actual_fd)
    { // static method
        std::string res("actual-fd ");
        res += std::to_string(actual_fd);
        if (actual_fd < 0)
            return(res);
    
        res += ", fd_flags ";
        int getfd_flags = fcntl(actual_fd, F_GETFD, (int) 0);
        res += fdlFlagsToStr(getfd_flags);

        res += ", fl_flags ";
        int getfl_flags = fcntl(actual_fd, F_GETFL, (int) 0);
        res += fdlFlagsToStr(getfl_flags);

        return(res);
    }
    
        
    #endif // of ifdef DEBUG

    
    EmEventFd * EmEventFd::getFromEmEventCPtr(EmEvent * eme_cptr)
    {
        EmEventFd * res = dynamic_cast<EmEventFd *>(eme_cptr);
        if ((eme_cptr) && (!res))
            PS_LOG_DEBUG_ARGS("Attempt to get EmEventFd ptr for EmEvent %p",
                              eme_cptr);

        return(res);
    }

    EmEventFd * EmEventFd::getFromEmEventCPtrNoLogIfNull(EmEvent * eme_cptr)
    {
        EmEventFd * res = dynamic_cast<EmEventFd *>(eme_cptr);
        return(res);
    }

    

    EmEventFd::EmEventFd(uint64_t initval) : EmEventCtr(initval)
    {
    }

    EmEventFd * EmEventFd::make_new(unsigned int initval,
                              // For setfd and setfl arg:
                              //   F_SETFDL_NOTHING - change nothing
                              //   Zero or pos number that is not
                              //   F_SETFDL_NOTHING - set flags to value of
                              //   arg, and clear any other flags
                              //   Neg number that is not F_SETFDL_NOTHING
                              //   - set flags that are set in (0 - arg),
                              //   but don't clear any flags
                              int f_setfd_flags,   // e.g. FD_CLOEXEC
                              int f_setfl_flags)   // e.g. O_NONBLOCK
    {
        PS_TIMEDBG_START_ARGS("initval %u, fd_flags %s, fl_flags %s",
                              initval, 
                              fdlFlagsToStr(f_setfd_flags).c_str(),
                              fdlFlagsToStr(f_setfl_flags).c_str());

        EmEventFd * emefd = new EmEventFd(initval);
        if (!emefd)
            return(NULL);
        DBG_NEW_EMV(emefd);

        if (!(f_setfl_flags & O_NONBLOCK))
        {
            emefd->makeBlocking();
            PS_LOG_DEBUG_ARGS("EmEventFd %p blocking", emefd);
        }
        else
        {
            PS_LOG_DEBUG_ARGS("EmEventFd %p nonblocking", emefd);
        }
        

        PS_LOG_DEBUG_ARGS("EmEventFd created %p, %s, initval %u",
                          emefd,
                          emefd->isBlocking() ? "blocking" : "nonblocking",
                          initval);

        EmEvent * eme_res = emefd->init(-1, 0, // EMV_xxx flags
                                               f_setfd_flags, f_setfl_flags);

        if (!eme_res)
        {
            delete emefd;
            DBG_DELETE_EMV(emefd);
            emefd = NULL;
        }
        
        return(emefd);
    }
    

/* ------------------------------------------------------------------------- */

    EmEventTmrFd * EmEventTmrFd::getFromEmEventCPtr(EmEvent * eme_cptr)
    {
        EmEventTmrFd * res = dynamic_cast<EmEventTmrFd *>(eme_cptr);
        if ((eme_cptr) && (!res))
            PS_LOG_DEBUG_ARGS("Attempt to get EmEventTmrFd ptr for EmEvent %p",
                              eme_cptr);

        return(res);
    }

    EmEventTmrFd * EmEventTmrFd::getFromEmEventCPtrNoLogIfNull(
                                                            EmEvent * eme_cptr)
    {
        EmEventTmrFd * res = dynamic_cast<EmEventTmrFd *>(eme_cptr);
        return(res);
    }

EmEventTmrFd::EmEventTmrFd(clockid_t clock_id,
                           EventMethEpollEquivImpl * emee/*may be NULL*/) :
    EmEventCtr(0 /*initval*/)
    {
        switch(clock_id)
        {
        case CLOCK_REALTIME:
        #ifdef __linux__
        case CLOCK_REALTIME_ALARM:
        case CLOCK_REALTIME_COARSE:
        case CLOCK_TAI:
        #endif
            PS_LOG_WARNING_ARGS("Realtime clock not supported, clock_id %u",
                              clock_id);
            throw std::invalid_argument(
                "clock_id realtime clock not supported");
            break;

        case CLOCK_MONOTONIC:
        #ifndef _IS_BSD
        // CLOCK_MONOTONIC_RAW not defined on FreeBSD13.3 and OpenBSD 7.3
        case CLOCK_MONOTONIC_RAW:
        #endif
        #ifdef __APPLE__
        case CLOCK_MONOTONIC_RAW_APPROX:
        case CLOCK_UPTIME_RAW:
        case CLOCK_UPTIME_RAW_APPROX:
        #endif
        #ifdef __linux__
        case CLOCK_MONOTONIC_COARSE:
        case CLOCK_BOOTTIME:
        case CLOCK_BOOTTIME_ALARM:
        #endif
            // We treat all these as CLOCK_MONOTONIC
            break;

        case CLOCK_PROCESS_CPUTIME_ID:
            PS_LOG_WARNING("CLOCK_PROCESS_CPUTIME_ID not supported");
            throw std::invalid_argument(
                "clock_id = CLOCK_PROCESS_CPUTIME_ID not supported");
            break;
            
        case CLOCK_THREAD_CPUTIME_ID:
            PS_LOG_WARNING("CLOCK_THREAD_CPUTIME_ID not supported");
            throw std::invalid_argument(
                "clock_id = CLOCK_THREAD_CPUTIME_ID not supported");
            break;

        default:
            PS_LOG_WARNING_ARGS("Unrecognized clock_id %u", clock_id);
            throw std::invalid_argument("Unrecognized clock_id");
            break;
        }

        event_meth_epoll_equiv_impl_ = emee;
    }

    // macOS claims to support a number of clocks - see "man clock_gettime" for
    // the list. In practice, pistache seems to use only CLOCK_MONOTONIC. For
    // now we accept any of the monotonic system-wide clocks, and reject any
    // others - in particular, we reject CLOCK_REALTIME (which can change
    // value) as well as the thread and process time clocks.
    // 
    // Consistent with not supporting CLOCK_REALTIME, we do not support
    // TFD_TIMER_CANCEL_ON_SET nor do we have to support discontinuous changes
    // in the clock value
    //
    // If emee is NULL here, it will need to be supplied when settime is
    // called
    EmEventTmrFd * EmEventTmrFd::make_new(clockid_t clock_id,// static function
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
                              EventMethEpollEquivImpl * emee/*may be NULL*/)
    {
        PS_TIMEDBG_START_ARGS("clock_id %u, fd_flags %s, fl_flags %s, ",
                              clock_id,
                              fdlFlagsToStr(f_setfd_flags).c_str(),
                              fdlFlagsToStr(f_setfl_flags).c_str());

        EmEventTmrFd * emefd = new EmEventTmrFd(clock_id, emee);
        if (!emefd)
            return(NULL);
        DBG_NEW_EMV(emefd);

        if (!(f_setfl_flags & O_NONBLOCK))
        {
            emefd->makeBlocking();
            PS_LOG_DEBUG_ARGS("EmEventTmrFd %p blocking", emefd);
        }
        else
        {
            PS_LOG_DEBUG_ARGS("EmEventTmrFd %p nonblocking", emefd);
        }
        

        PS_LOG_DEBUG_ARGS("EmEventTmrFd created %p, emee %p, %s, clock_id %u",
                          emefd, emee,
                          emefd->isBlocking() ? "blocking" : "nonblocking",
                          clock_id);

        EmEvent * eme_res = emefd->init(-1, EVM_READ,
                                               f_setfd_flags, f_setfl_flags);

        if (!eme_res)
        {
            delete emefd;
            DBG_DELETE_EMV(emefd);
            emefd = NULL;
        }
        
        return(emefd);
    }

    // settime is analagous to timerfd_settime in linux
    // 
    // The linux flags TFD_TIMER_ABSTIME and TFD_TIMER_CANCEL_ON_SET are not
    // supported
    //
    // Since pistache doesn't use the "struct itimerspec * old_value" feature
    // of timerfd_settime, we haven't implemented that feature.
    // 
    // If new_timeval_cptr is NULL or *new_timeval_cptr is all zero, the
    // settime call will reset the timer (again as per timerfd_settime)
    // 
    // Note: settime is in EmEvent rather than solely in EmEventTmrFd since any
    // kind of event may have a timeout set, not only timer events
    int EmEventTmrFd::settime(
        const std::chrono::milliseconds * new_timeval_cptr,
        EventMethEpollEquivImpl * emee/*may be NULL*/)
    {
        uint64_t old_counter_val = resetCounterVal();
        if (old_counter_val)
        {
            PS_LOG_DEBUG_ARGS("EmEventTmrFd %p settime zeroed expiry counter",
                              this);
        }

        setPriorTv(new_timeval_cptr);

        if (emee)
        {
            if (emee != event_meth_epoll_equiv_impl_)
            {
                if (event_meth_epoll_equiv_impl_)
                {
                    PS_LOG_WARNING_ARGS("EmEventTmrFd %p EMEE can't be "
                                        "changed, old %p, new %p",
                                        this, event_meth_epoll_equiv_impl_,
                                        emee);
                    throw std::invalid_argument(
                        "EmEventTmrFd EMEE cannot be changed");
                    // Note: This could be implemented if needed - allow
                    // EmEvent to belong to multiple EMEE
                }
                event_meth_epoll_equiv_impl_ = emee;
            }
        }
        else if (event_meth_epoll_equiv_impl_)
        {
            emee = event_meth_epoll_equiv_impl_;
        }

        if ((new_timeval_cptr) && (new_timeval_cptr->count()))
        {
            // We HAVE to add to an EMEE now, since that's how the timer starts
            // running
            
            if (!emee)
            {
                PS_LOG_INFO_ARGS(
                    "EmEventTmrFd %p null EMEE for starting timer", this);
                // This isn't perhaps ideal, in that the timer won't start
                // running until the timer can be added to an EMEE. However it
                // is needed in that TimerPool creates timers and only later
                // connects each of them to an EMEE via a call to
                // TimerPool::Entry::registerReactor (the reactor owns the
                // EMEE).
                
                add_was_artificial_ = false;
                return(0);
            }

            int ctl_res = emee->ctlEx(EvCtlAction::Add,
                           this,
                           flags_ /* events*/,
                           NULL /* timeval_cptr - use prior_tv_*/,
                           true/*forceEmEventCtlOnly*/);
            if (ctl_res != 0)
            {
                PS_LOG_INFO_ARGS("EmEventTmrFd %p failed to EvCtlAction::Add",
                                 this);
                throw std::runtime_error("EvCtlAction::Add failed");
            }
            add_was_artificial_ = true;
        }
        else if (ev_)
        {
            if (emee)
            {
                int ctl_res = emee->ctlEx(EvCtlAction::Del,
                                          this,
                                          0 /* events*/,
                                          NULL /*timeval_cptr*/,
                                          true/*forceEmEventCtlOnly*/);
                if (ctl_res != 0)
                {
                    PS_LOG_INFO_ARGS(
                        "EmEventTmrFd %p failed to EvCtlAction::Del ev_ %p",
                        this, ev_);
                    throw std::runtime_error("EvCtlAction::Del failed");
                }
                add_was_artificial_ = false;

                // Note: It should not be necessary to delete and free
                // ev_. Removing it via ctl should be sufficient; a later call
                // to settime can add the same ev_ with ctl again, using a
                // different timeout for the add, and all will be well
            }
        }

        return(0);
    }

    void EmEventTmrFd::handleEventCallback(short & ev_flags_in_out)
    {
        if (ev_flags_in_out & EV_TIMEOUT)
        {
            PS_LOG_DEBUG_ARGS("EmEventTmrFd %p increment expiry counter", 
                              this);
            writeProt(1);
        }

        if (flags_ & EVM_READ)
            ev_flags_in_out |= EV_READ;
    }
    
    ssize_t EmEventTmrFd::write([[maybe_unused]] const void * buf,
                                [[maybe_unused]] size_t count)
    {
        PS_LOG_DEBUG("Cannot write to an EmEventTmrFd");
        
        errno = EBADF; // "not open for writing"
        return(-1);
    }
    
        

/* ------------------------------------------------------------------------- */
    
    // Returns true if "pending" (i.e. has been added in libevent)
    // events is any of EV_TIMEOUT|EV_READ|EV_WRITE|EV_SIGNAL
    // If tv is non-null and event has time out, *tv is set to it
    // Returns true if pending (i.e. added) on events, zero otherwise
    bool EmEvent::eventPending(short events, struct timeval *tv)
    {
        if (!ev_)
            return(false);
        
        return(event_pending(ev_, events, tv) != 0);
    }

    // Returns true if "ready" (i.e. matching ready_flags has been set)
    // events is any of EV_TIMEOUT|EV_READ|EV_WRITE|EV_SIGNAL
    // Returns true if ready on events, false otherwise
    bool EmEvent::eventReady(short events)
    {
        short ready_flags = getReadyFlags();

        return((ready_flags & events) != 0);
    }


    EmEvent * EmEvent::make_new(int actual_fd,
                                  short flags,
                                  // For setfd and setfl arg:
                                  //   F_SETFDL_NOTHING - change nothing
                                  //   Zero or pos number that is not
                                  //   F_SETFDL_NOTHING - set flags to value of
                                  //   arg, and clear any other flags
                                  //   Neg number that is not F_SETFDL_NOTHING
                                  //   - set flags that are set in (0 - arg),
                                  //   but don't clear any flags
                                  int f_setfd_flags, // e.g. FD_CLOEXEC
                                  int f_setfl_flags  // e.g. O_NONBLOCK
        )
    { // static method
        PS_TIMEDBG_START_ARGS("actual_fd %d, evm_flags %s, "
                              "fd_flags %s, fl_flags %s",
                              actual_fd,
                              evmFlagsToStdString(flags)->c_str(),
                              fdlFlagsToStr(f_setfd_flags).c_str(),
                              fdlFlagsToStr(f_setfl_flags).c_str());

        EmEvent * eme = new EmEvent();
        if (!eme) 
            return(NULL);
        DBG_NEW_EMV(eme);

        PS_LOG_DEBUG_ARGS("EmEvent created %p", eme);

        // NB: don't pass EVM_TIMEOUT as a flag; the presence of a timeout is
        // indicate solely by the presence of non-zero timeout period
        
        return(eme->init(actual_fd, flags, f_setfd_flags, f_setfl_flags));
    }
    
        
    EmEvent * EmEvent::init(int actual_fd,
                                  short flags,
                                  // For setfd and setfl arg:
                                  //   F_SETFDL_NOTHING - change nothing
                                  //   Zero or pos number that is not
                                  //   F_SETFDL_NOTHING - set flags to value of
                                  //   arg, and clear any other flags
                                  //   Neg number that is not F_SETFDL_NOTHING
                                  //   - set flags that are set in (0 - arg),
                                  //   but don't clear any flags
                                  int f_setfd_flags, // e.g. FD_CLOEXEC
                                  int f_setfl_flags  // e.g. O_NONBLOCK
        )
    {
        PS_TIMEDBG_START;

        if (flags & EVM_SIGNAL)
        {
            if (flags & (EVM_READ | EVM_WRITE))
            {
                PS_LOG_WARNING("event for signal, but also read/write");
                throw std::invalid_argument(
                    "event for signal, but also read/write - can't be both");
            }

            if (actual_fd == -1)
            {
                PS_LOG_WARNING("actual_fd not set, must be a signal number");
                throw std::invalid_argument(
                    "actual_fd not set, must be a signal number");
            }

            PS_LOG_DEBUG_ARGS("EmEvent %p for signal %d", this, actual_fd);
        }

        // We defer the creation of the libevent event until ctl Add/Mod is
        // called, so in the case where the ctl call has different flags we
        // don't have to remake libevent's struct event. Similarly, if we need
        // to open our own actual_fd (because actual_fd is -1 here and this is
        // to be a read and/or write socket), we do it in ctl not here.

        requested_f_setfd_flags_ = f_setfd_flags;
        requested_f_setfl_flags_ = f_setfl_flags;

        requested_actual_fd_ = actual_fd;

        // setFdlFlagsIfNeededAndActualFd will set fd_flags and fl_flags on
        // actual_fd provided actual_fd is a positive value; otherwise, setting
        // fd_flags and fl_flags will be done out of ctl Add/Mod if and when a
        // socket has been openned
        setFdlFlagsIfNeededAndActualFd(actual_fd);

        setFlags(flags);
        resetReadyFlags();

        PS_LOG_DEBUG_ARGS("Initialized EmEvent %p, actual_fd %d",
                          this, actual_fd);

        return(this);
    }

    EmEvent::EmEvent() : ev_(NULL), flags_(0),
                         add_was_artificial_(false),
                         event_meth_epoll_equiv_impl_(NULL), // parent
                         ready_flags_(0),
                         user_data_(0),
                         requested_f_setfd_flags_(F_SETFDL_NOTHING),
                         requested_f_setfl_flags_(F_SETFDL_NOTHING),
                         requested_actual_fd_(-1),
                         prior_tv_cptr_(NULL) // ptr to timeout value
    {
        memset(&prior_tv_, 0, sizeof(prior_tv_));
        
        INC_DEBUG_CTR(em_event);
    }

    void EmEvent::setPriorTv(const std::chrono::milliseconds * timeval_cptr)
    {
        memset(&prior_tv_, 0, sizeof(prior_tv_));
        prior_tv_cptr_ = NULL;

        if (timeval_cptr)
        {
            if (timeval_cptr->count() < 1000)
                prior_tv_.tv_usec = (suseconds_t) std::chrono::
               duration_cast<std::chrono::microseconds>(*timeval_cptr).count();
            else
                prior_tv_.tv_sec = (time_t) (std::chrono::
                   duration_cast<std::chrono::seconds>(*timeval_cptr).count());
            prior_tv_cptr_ = &prior_tv_;
        }
    }
    

    // settime can be used to configure the timeout prior to calling ctl/Add
    //
    // For EmEventTmrFd, settime is analagous to timerfd_settime in linux
    // 
    // The linux flags TFD_TIMER_ABSTIME and TFD_TIMER_CANCEL_ON_SET are not
    // supported
    // 
    // Since pistache doesn't use the "struct itimerspec * old_value" feature
    // of timerfd_settime, we haven't implemented that feature.
    //
    // Note: unlike in EmEventTmrFd::settime, where if new_timeval_cptr is NULL
    // or *new_timeval_cptr is zero the settime call will reset the timer per
    // timerfd_settime, here the timer is not specially reset even if
    // new_timeval_cptr is NULL or *new_timeval_cptr is zero
    //
    // Note: settime is in EmEvent rather than solely in EmEventTmrFd since any
    // kind of event may have a timeout set, not only EmEventTmrFd timer events
    int EmEvent::settime(const std::chrono::milliseconds * new_timeval_cptr,
                         EventMethEpollEquivImpl * emee/*may be NULL*/)
    {
        if ((ev_) && (event_meth_epoll_equiv_impl_))
        {
            PS_LOG_WARNING("trying to settime after ev_ created and EMEE "
                         "assigned");
            throw std::runtime_error(
                "trying to settime after ev_ created and EMEE assigned");
            // It would be possible to implement this by swapping out the ev_,
            // but seems not needed at present
        }

        if (emee)
        {
            if (emee != event_meth_epoll_equiv_impl_)
            {
                if (event_meth_epoll_equiv_impl_)
                {
                    PS_LOG_WARNING_ARGS("EmEventTmrFd %p EMEE can't be "
                                        "changed, old %p, new %p",
                                        this, event_meth_epoll_equiv_impl_,
                                        emee);
                    throw std::invalid_argument(
                        "EmEventTmrFd EMEE cannot be changed");
                }
                event_meth_epoll_equiv_impl_ = emee;
            }
        }
        else if (event_meth_epoll_equiv_impl_)
        {
            emee = event_meth_epoll_equiv_impl_;
        }
        
        setPriorTv(new_timeval_cptr);

        return(0); // success
    }
    
    // Always use this, don't set flags_ directly
    void EmEvent::setFlags(short flgs)
    {
        PS_TIMEDBG_START;
        
        // Mask out EVM_TIMEOUT - a flag that may be set in ready_flags_ if a
        // timeout occurs, but which is not needed to get a timeout.
        flgs &= ~EVM_TIMEOUT;

        if ((event_meth_epoll_equiv_impl_) && (flgs & (EVM_CLOSED | EVM_ET)))
        {
            int base_features =
                event_meth_epoll_equiv_impl_->getEventBaseFeatures();
            
            if ((flgs & EVM_ET) && (!(base_features & EV_FEATURE_ET)))
            {
                PS_LOG_INFO("No edge trigger");
                throw std::system_error(EOPNOTSUPP,
                                   std::generic_category(), "No edge trigger");
            }

            if ((flgs & EVM_CLOSED) &&
                (!(base_features & EV_FEATURE_EARLY_CLOSE)))
            {
                PS_LOG_INFO("No early close");
                throw std::system_error(EOPNOTSUPP,
                                    std::generic_category(), "No early close");
                // !!!! If early close is not supported, rather than throwing
                // here, when a close is initiated, we should likely loop to
                // read all outstanding data on the event and then do the close
            }
        }

        flags_ = flgs;
    }

    int EmEvent::disarm()
    {
        PS_TIMEDBG_START;
        
        if (ev_ == NULL)
            return(0); // nothing to do

        // Note. If the event has already executed or has never been added,
        // event_del will have no effect (i.e. is harmless).
        // Note: Don't use finalize here, this Fd may be armed again later
        int event_del_res = TRY_RET(event_del(ev_));
        return(event_del_res);
    }

    int EmEvent::close() // disarms as well as closes
    {
        PS_TIMEDBG_START_THIS;

        em_socket_t actual_fd = -1; // em_socket_t is type int
        int finalize_res = 0;

        if (ev_ == NULL)
        {
            actual_fd = requested_actual_fd_;
        }
        else
        {
            actual_fd = event_get_fd(ev_);

            // See earlier comment: Why and how we use libevent's finalize
            PS_LOG_DEBUG_ARGS("About to finalize+free ev_ %p of EmEvent %p",
                              ev_, this);

            auto old_ev = ev_;
            ev_ = 0;

            finalize_res = event_free_finalize(0,//reserved
                                    old_ev, libevEventFinalizeAndFreeCallback);

            PS_LOG_DEBUG_ARGS("finalize_res %d, ev_ %p", finalize_res, old_ev);

            // Deliberately leak ev_ if finalize_res = -1

            // Note: libevEventFinalizeAndFreeCallback does
            // DEC_DEBUG_CTR(libevent_event)
        }

        requested_actual_fd_ = -1;

        int actual_fd_close_res = 0;
        if (actual_fd > 0)
        {
            PS_LOG_DEBUG_ARGS("::close actual_fd %d", actual_fd);
            actual_fd_close_res = ::close(actual_fd);
        }

        if (finalize_res < 0)
        {
            PS_LOG_DEBUG_ARGS("event_del failed, ev_ %p", ev_);
            return(-1);
        }
        else if (actual_fd_close_res < 0)
        {
            PS_LOG_INFO_ARGS("::close failed, actual_fd %d", actual_fd);
            return(-1);
        }

        return(0);
    }

    EventMethEpollEquivImpl * EmEvent::getEventMethEpollEquivImpl()
    {
        EventMethEpollEquivImpl * tmp_event_meth_epoll_equiv =
                                                  event_meth_epoll_equiv_impl_;
        if (!tmp_event_meth_epoll_equiv)
            return(NULL);

        EventMethEpollEquivImpl * found_emee = EventMethEpollEquivImpl::
             getEventMethEpollEquivImplFromEmeeSet(tmp_event_meth_epoll_equiv);
        
        if (!found_emee)
        {
            PS_LOG_DEBUG_ARGS("EmEvent %p has "
                              "EventMethEpollEquivImpl %p "
                              "unexpectedly not in emee_cptr_set, "
                              "nulling out event_meth_epoll_equiv_impl_",
                this, tmp_event_meth_epoll_equiv);
            event_meth_epoll_equiv_impl_ = NULL;
            return(NULL);
        }

        if (found_emee != tmp_event_meth_epoll_equiv)
        {
            PS_LOG_DEBUG_ARGS("found_emee %p != tmp_event_meth_epoll_equiv %p",
                              found_emee, tmp_event_meth_epoll_equiv);
            
            assert(found_emee == tmp_event_meth_epoll_equiv);
            return(NULL);
        }
        
        return(tmp_event_meth_epoll_equiv);
    }

    // As well as setting event_meth_epoll_equiv_impl_ to NULL, we must release
    // ev_ (the libevent event) which effectively holds a reference to the
    // libevent event_base class, which is in EventMethBase, which in turn is
    // in EventMethEpollEquiv. So if EventMethEpollEquiv goes out of scope,
    // then ev_ needs to be freed
    void EmEvent::detachEventMethEpollEquiv()
    {
        if (ev_)
        {
            // See earlier comment: Why and how we use libevent's finalize
            PS_LOG_DEBUG_ARGS("About to finalize+free ev_ %p of EmEvent %p",
                              ev_, this);

            auto old_ev = ev_;
            ev_ = 0;

            #ifdef DEBUG
            int ev_free_finalize_initial_res =
            #endif
                event_free_finalize(0,//reserved
                                    old_ev, libevEventFinalizeAndFreeCallback);

            PS_LOG_DEBUG_ARGS("ev_free_finalize_initial_res %d, ev_ %p",
                              ev_free_finalize_initial_res, old_ev);

            // Note: libevEventFinalizeAndFreeCallback does
            // DEC_DEBUG_CTR(libevent_event)
        }

        event_meth_epoll_equiv_impl_ = NULL;
    }
    
        

    EmEvent::~EmEvent()
    {
        DEC_DEBUG_CTR(em_event);

        close();
    }

    int EmEvent::getActualFd() const // virtual
    {
        int actual_fd = getActualFdPrv();
        #ifdef DEBUG
        if (actual_fd < 0)
        {
            PS_LOG_INFO_ARGS("EmEvent %p has negative actual_fd?", this);
            PS_LOGDBG_STACK_TRACE; 
        }
        #endif
        
        return(actual_fd);
    }

    ssize_t EmEvent::read(void * buf, size_t count) // virtual
    {
        return(::read(getActualFd(), buf, count));
    }
    
    ssize_t EmEvent::write(const void * buf, size_t count) // virtual
    {
        return(::write(getActualFd(), buf, count));
    }

    int EmEvent::getActualFdPrv() const
    {
        int actual_fd = ((ev_) ? event_get_fd(ev_) : requested_actual_fd_);

        #ifdef DEBUG
        if (actual_fd >= 0)
        {
            if (getEmEventType() == EmEvTimer)
            {
                PS_LOG_INFO_ARGS("Timer EmEvent %p has non-neg actual_fd?",
                                  this);
                throw std::runtime_error("Non negative actual_fd for timer");
            }
            else if (getEmEventType() == EmEvEventFd)
            {
                PS_LOG_INFO_ARGS(
                            "eventfd EmEvent %p has non-neg actual_fd?", this);
                throw std::runtime_error(
                                 "Non negative actual_fd for eventfd EmEvent");
            }
        }
        
        #endif

        return(actual_fd);
    }
    
    int EmEvent::getActualFd(const EmEvent * em_ev) // static version
    {
        if (em_ev == NULL)
            return(-1);
        
        return(em_ev->getActualFd());
    }

    #ifdef DEBUG
    static std::shared_ptr<std::string> ctlActionToStr(
                                      EvCtlAction op)
    {
        std::shared_ptr<std::string> res(std::make_shared<std::string>(""));

        switch(op)
        {
        case EvCtlAction::Add:
            (*res) += "Add";
            break;

        case EvCtlAction::Mod:
            (*res) += "Mod";
            break;

        case EvCtlAction::Del:
            (*res) += "Del";
            break;

        default:
            (*res) += "Unknown ctl action";
            break;
        }

        return(res);
    }
    #endif // of ifdef DEBUG

    void EmEvent::setFdlFlagsHelper(int actual_fd,
                                    int get_cmd, // F_GETFD or F_GETFL
                                    int set_cmd, // F_SETFD or F_SETFL
                                    int f_setfdl_flags)
    {
        PS_TIMEDBG_START;
        
        if (f_setfdl_flags == F_SETFDL_NOTHING)
            return;

        if (f_setfdl_flags >= 0)
        {
            if ((actual_fd != -1) || (f_setfdl_flags != 0))
            {
                // Note: If *none* of EVM_READ, EVM_WRITE and EVM_SIGNAL were
                // set, then the the event can be triggered only by a timeout
                // or by manual activation; in that case, actual_fd will still
                // be -1 and we cannot set cntl flags
                if (actual_fd == -1)
                {
                    PS_LOG_INFO("actual_fd not set");
                    throw std::invalid_argument("actual_fd not set");
                }

                int fcntl_res = fcntl(actual_fd, set_cmd, f_setfdl_flags);
                if (fcntl_res == -1)
                {
                    PS_LOG_INFO("fcntl set failed");
                    throw std::runtime_error("fcntl set failed");
                }
            }
        }
        else
        {
            if (actual_fd == -1) // see note above on actual_fd of -1
            {
                PS_LOG_INFO("actual_fd not set");
                throw std::invalid_argument("actual_fd not set");
            }

            int old_setfdl_flags = fcntl(actual_fd, get_cmd, (int) 0);
            f_setfdl_flags = (0 - f_setfdl_flags);
            if (old_setfdl_flags != f_setfdl_flags)
            {
                f_setfdl_flags |= old_setfdl_flags;
                int fcntl_res = fcntl(actual_fd, set_cmd, f_setfdl_flags);
                if (fcntl_res == -1)
                {
                    PS_LOG_INFO("fcntl set failed");
                    throw std::runtime_error("fcntl set failed");
                }
            }
        }
    }


    void EmEvent::setFdlFlagsIfNeededAndActualFd(int actual_fd)
    {
        if (actual_fd < 0)
            return;
        // Note: If *none* of EVM_READ, EVM_WRITE and EVM_SIGNAL are set, then
        // the event can be triggered only by a timeout or by manual
        // activation; in that case, actual_fd will still be -1

        PS_LOG_DEBUG_ARGS("EmEvent %p, ev_ libev %p, actual_fd %d",
                          this, ev_, actual_fd);

        if (requested_f_setfd_flags_ != F_SETFDL_NOTHING)
        {
            setFdlFlagsHelper(actual_fd, F_GETFD, F_SETFD,
                                 requested_f_setfd_flags_);
            requested_f_setfd_flags_ = F_SETFDL_NOTHING;
        }

        if (requested_f_setfl_flags_ != F_SETFDL_NOTHING)
        {
            setFdlFlagsHelper(actual_fd, F_GETFL, F_SETFL,
                                 requested_f_setfl_flags_);
            requested_f_setfl_flags_ = F_SETFDL_NOTHING;
        }
    }

    #ifdef DEBUG
    static const char * emEventTypeToStr(EmEventType em_ev_type)
    {
        switch(em_ev_type)
        {
        case EmEvNone:
            return("None");

        case EmEvReg:
            return("Regular");

        case EmEvEventFd:
            return("eventfd");
            
        case EmEvTimer:
            return("Timer");

        default:
            return("Unknown");
        }
    }
    #endif

    int EmEvent::ctl(
                EvCtlAction op, // add, mod, or del
                EventMethEpollEquivImpl * emee,
                short     events, // bitmask of EVM_... events
                std::chrono::milliseconds * timeval_cptr)
    {
        PS_TIMEDBG_START_ARGS("EmEvent (this) %p, EMEE %p, "
                              "EvCtlAction %s, "
                              "EmEvent type %s, "
                              "events %s, timeval %dms, prior_tv_ %ds %dms",
                              this, emee,
                              ctlActionToStr(op)->c_str(),
                              emEventTypeToStr(getEmEventType()),
                              evmFlagsToStdString(events)->c_str(),
                              timeval_cptr ? timeval_cptr->count() : -1,
                              prior_tv_cptr_ ? prior_tv_cptr_->tv_sec : -1,
                              prior_tv_cptr_ ?
                                        ((prior_tv_cptr_->tv_usec)/1000) : -1);
        #ifdef DEBUG
        if (getEmEventType() != EmEvReg)
            PS_LOG_DEBUG_ARGS("EmEvent %p type is %s",
                              this, emEventTypeToStr(getEmEventType()));
        #endif

        if (emee)
        {
            EventMethEpollEquivImpl * prior_emee= event_meth_epoll_equiv_impl_;
            if (prior_emee != emee)
            {
                PS_LOG_DEBUG_ARGS("Set event_meth_epoll_equiv_impl_, "
                                  "old val %p%s, new %p",
                                  prior_emee, prior_emee ? " (NOT NULL)" : "",
                                  emee);
                if (prior_emee)
                {
                    // Check to se if the EmEvent is already in a different
                    // interest_ list
                    Pistache::EventMethEpollEquivImpl * owning_emee = NULL;
                    EmEvent * dummy_em_event =
                        EventMethEpollEquivImpl::findEmEventInAnInterestSet(
                                                           this, &owning_emee);
                    if ((dummy_em_event) && (owning_emee) &&
                        (owning_emee != emee))
                    {
                        PS_LOG_INFO_ARGS("Unsupported emee change for fd %p, "
                                         "prior_emee %p, owning_emee %p, "
                                         "emee %p",
                                         this, prior_emee, owning_emee, emee);
                        throw std::runtime_error("Unsupported emee change");
                    }
                }
                
                event_meth_epoll_equiv_impl_ = emee;
            }
        }
        else
        {
            Pistache::EventMethEpollEquivImpl * owning_emee = NULL;
            #ifdef DEBUG
            [[maybe_unused]]
            #endif
            EmEvent * dummy_em_event =
                EventMethEpollEquivImpl::findEmEventInAnInterestSet(this,
                                                                &owning_emee);
            PS_LOG_INFO_ARGS("EmEvent %p ctl call has null emee, "
                              "owning_emee %p", this, owning_emee);

            if ((dummy_em_event) && (owning_emee))
            {
                emee = owning_emee; // try and recover
            }
            else
            {
                PS_LOG_INFO("emee null and owning_emee null");
                throw std::invalid_argument("emee null and owning_emee null");
            }

            event_meth_epoll_equiv_impl_ = emee;
        }
        
        int actual_fd = getActualFdPrv();

        struct timeval tv;
        memset(&tv, 0, sizeof(tv));
        struct timeval * tv_cptr = NULL;
        if (timeval_cptr)
        {
            if (timeval_cptr->count() < 1000)
                tv.tv_usec = (suseconds_t) std::chrono::
               duration_cast<std::chrono::microseconds>(*timeval_cptr).count();
            else
                tv.tv_sec = (time_t) (std::chrono::
                   duration_cast<std::chrono::seconds>(*timeval_cptr).count());
            tv_cptr = &tv;
        }
        else
        {
            tv_cptr = prior_tv_cptr_;
        }

        if ((op == EvCtlAction::Add) ||
            (op == EvCtlAction::Mod))
        {
            if (!ev_)
            {
                // Since we had not created ev_ previously, we don't have to be
                // concerned about what flags were previously on ev_ - we can
                // ignore any flags previously specified (e.g. on event
                // make_new) and simply use the flags being specified for this
                // call. I think.
                setFlags(events); // sets flags_ = events

                if ((actual_fd == -1) && (requested_actual_fd_ != -1))
                {
                    actual_fd = requested_actual_fd_;
                    requested_actual_fd_  = -1;
                }

                if ((actual_fd == -1) && (flags_ & (EVM_READ | EVM_WRITE)) &&
                    ((flags_ & EVM_WRITE) ||
                     ((flags_ & EVM_READ) && (getEmEventType() == EmEvReg))))
                {
                    PS_LOG_INFO_ARGS("EmEvent %p, no actual fd (ctl error)",
                                      this);
                    
                    errno = EBADF;
                    return(-1);
                }

                ev_ = TRY_NULL_RET(
                    event_new(
                        event_meth_epoll_equiv_impl_->getEventMethBase()->
                                                                getEventBase(),
                        actual_fd,
                        flags_ | EV_FINALIZE, // See earlier comment: Why and
                                              // how we use libevent's finalize
                        eventCallbackFn,
                        (void *)this
                        /*final arg here is passed to callback as "arg"*/));

                if (!ev_)
                {
                    PS_LOG_DEBUG("libev event_new returned null");
                    return(-1);
                }

                INC_DEBUG_CTR(libevent_event);

                PS_LOG_DEBUG_ARGS("EmEvent %p libevent ev_ %p via event_new, "
                                  "actual_fd %d",
                                  this, ev_, actual_fd);
                
            }
            else if ((events) && (events != flags_))
            {
                // Caller is specifying different events to the ones specified
                // when libevent's event was created previously. Since for
                // libevent the event mask is set by event_new, we'll now need
                // to dump the old libevent event and replace it with a new one
                // that has different event mask.

                if (!event_meth_epoll_equiv_impl_)
                {
                    PS_LOG_INFO("event_meth_epoll_equiv_impl_ null");
                    throw std::runtime_error(
                        "event_meth_epoll_equiv_impl_ null");
                }

                // See earlier comment: Why and how we use libevent's finalize
                PS_LOG_DEBUG_ARGS("About to finalize+free ev_ %p, EmEvent %p",
                                  ev_, this);

                struct event * old_ev = ev_;
                ev_ = NULL;

                #ifdef DEBUG
                int ev_free_finalize_initial_res =
                #endif
                    event_free_finalize(0,//reserved
                                    old_ev, libevEventFinalizeAndFreeCallback);
                // Note libevEventFinalizeAndFreeCallback does
                // DEC_DEBUG_CTR(libevent_event)

                PS_LOG_DEBUG_ARGS("ev_free_finalize_initial_res %d, ev_ %p",
                                  ev_free_finalize_initial_res, old_ev);

                struct event * replacement_ev = event_new(
                    event_meth_epoll_equiv_impl_->getEventMethBase()->
                                                                getEventBase(),
                    actual_fd, // keep same actual fd, if any
                    events | EV_FINALIZE, // See earlier comment: Why and how
                                          // we use libevent's finalize
                    eventCallbackFn,
                    (void *)this/*passed to callback as "arg"*/);
                if (!replacement_ev)
                {
                    PS_LOG_INFO("new replacement_ev is NULL");
                    throw std::runtime_error("new replacement_ev is NULL");
                }
                INC_DEBUG_CTR(libevent_event);

                PS_LOG_DEBUG_ARGS("Events changing for EmEvent %p, "
                                  "actual_fd %d, "
                                  "old events %s, new events %s, "
                                  "old libev ev_ %p, new ev_ %p",
                                  this, actual_fd,
                                  evmFlagsToStdString(flags_)->c_str(),
                                  evmFlagsToStdString(events)->c_str(),
                                  old_ev, replacement_ev);

                ev_ = replacement_ev;
                setFlags(events);
            }
        }

        if (tv_cptr)
        {
            if (tv_cptr != prior_tv_cptr_)
            {
                prior_tv_ = *tv_cptr;
                prior_tv_cptr_ = &prior_tv_;
            }
        }

        int ctl_res = -1;

        resetReadyFlags();

        // Note: Modification of EventMethEpollEquivImpl::interest_ and ready_
        // are handled in EventMethEpollEquivImpl::ctl after this EmEvent::ctl
        // has returned
        switch(op)
        {
        case EvCtlAction::Add:
            // Note: Although there is a flag EVM_TIMEOUT, it is not an input
            // to event_add; it is used when an event is ready, to indicate
            // whether a timeout occured on the event. Rather, the second parm
            // to event_add (which is const struct timeval *) indicates whether
            // timeout is needed
            // 
            // Note: In some SSL-code cases, pistache code calls setsockopt to
            // set a timer on a file descriptor directly. However, this doesn't
            // appear to happen for a file-desc that has an associated EmEvent;
            // for those file-descs, we can call event_add with timeout
            // specified. So long as there is no setsockopt for a timeout on an
            // actual-fd of an EmEvent, it shouldn't get too confused.

            ctl_res = event_add(ev_, tv_cptr);
            break;

        case EvCtlAction::Mod: // rearm
            // For a deactivated event, reactivated by adding again
            // 
            // Per libevent documentation, if the event is already active, it
            // remains active (aka pending); in that case, if tv_cptr is
            // non-null, the prior timeout (if any) is replaced by the new
            // timeout.
            ctl_res = event_add(ev_, tv_cptr);
            break;

        case EvCtlAction::Del:
            // Note event_del does nothing if event already inactive
            // Note: Don't use finalize here, this Fd may be armed again later
            ctl_res = event_del(ev_);
            break;

        default:
            PS_LOG_WARNING("Invalid EvCtlAction");
            errno = EINVAL;
            ctl_res = -1;
            break;
        }

        PS_LOG_DEBUG_ARGS("ctl_res (int) = %d", ctl_res);

        return(ctl_res);
    }
        
/* ------------------------------------------------------------------------- */

    int EventMethEpollEquivImpl::tcp_prot_num = -1;
    std::mutex EventMethEpollEquivImpl::tcp_prot_num_mutex;

    int EventMethEpollEquivImpl::getActualFd(const EmEvent * em_event)
    { // static
        // Returns -1 if no actual Fd
        return(EmEvent::getActualFd(em_event));
    }

    ssize_t EventMethEpollEquivImpl::writeEfd(EmEvent* efd, const uint64_t val)
    { // static
        EmEventFd * this_efd = EmEventFd::getFromEmEventCPtrNoLogIfNull(efd);
        if (!this_efd)
            return(-1);
        
        return(this_efd->write(val));
    }

    ssize_t EventMethEpollEquivImpl::readEfd(EmEvent * efd,
                                         uint64_t * val_out_ptr)
    { // static
        EmEventFd * this_efd = EmEventFd::getFromEmEventCPtrNoLogIfNull(efd);
        if (!this_efd)
            return(-1);
        
        return(this_efd->read(val_out_ptr));
    }

    ssize_t EventMethEpollEquivImpl::read(EmEvent* fd, void* buf, size_t count)
    { // static
        if (!fd)
        {
            PS_LOG_WARNING("Null fd");
            errno = EINVAL;
            return(-1);
        }
        
        return(fd->read(buf, count));
    }
    
    ssize_t EventMethEpollEquivImpl::write(EmEvent * fd,
                                       const void * buf, size_t count)
    { // static
        if (!fd)
        {
            PS_LOG_WARNING("Null fd");
            errno = EINVAL;
            return(-1);
        }
        
        return(fd->write(buf, count));
    }

    EmEvent * EventMethEpollEquivImpl::getAsEmEvent(EmEventFd * efd)
    { // static
        if (!efd)
            return(NULL);

        return(efd->getAsFd());
    }
    
    uint64_t EventMethEpollEquivImpl::getEmEventUserDataUi64(const EmEvent* fd)
    { // static
        if (!fd)
        {
            PS_LOG_WARNING("Null fd");
            throw std::runtime_error("Null fd");
        }

        return(fd->getUserDataUi64());
    }
    
    Fd EventMethEpollEquivImpl::getEmEventUserData(const EmEvent * fd)
    { // static
        if (!fd)
        {
            PS_LOG_WARNING("Null fd");
            throw std::runtime_error("Null fd");
        }
        
        return(fd->getUserData());
    }
    
    void EventMethEpollEquivImpl::setEmEventUserData(EmEvent * fd,
                                                 uint64_t user_data)
    { // static
        if (!fd)
        {
            PS_LOG_WARNING("Null fd");
            throw std::runtime_error("Null fd");
        }
        
        fd->setUserData(user_data);
    }

    // sets fd's user-data to fd
    void EventMethEpollEquivImpl::setEmEventUserData(EmEvent* fd, Fd user_data)
    { // static
        if (!fd)
        {
            PS_LOG_WARNING("Null fd");
            throw std::runtime_error("Null fd");
        }
        
        fd->setUserData((uint64_t)user_data);
    }

    void EventMethEpollEquivImpl::resetEmEventReadyFlags(EmEvent * fd)
    { // static
        if (!fd)
        {
            PS_LOG_WARNING("Null fd");
            throw std::runtime_error("Null fd");
        }
        
        fd->resetReadyFlags();
    }

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
    int EventMethEpollEquivImpl::setEmEventTime(EmEvent * fd,
                            const std::chrono::milliseconds * new_timeval_cptr,
                            EventMethEpollEquivImpl * emee/*may be NULL*/)
    { // static
        if (!fd)
        {
            PS_LOG_WARNING("Null fd");
            errno = EINVAL;
            return(-1);
        }

        return(fd->settime(new_timeval_cptr, emee));
    }

    EmEventType EventMethEpollEquivImpl::getEmEventType(EmEvent * fd)
    { // static
        if (!fd)
            return(EmEvNone);

        return(fd->getEmEventType());
    }

    int EventMethEpollEquivImpl::getTcpProtNum()
    {
        PS_TIMEDBG_START;
        
        if (tcp_prot_num != -1)
            return(tcp_prot_num);

        GUARD_AND_DBG_LOG(tcp_prot_num_mutex);
        if (tcp_prot_num != -1)
            return(tcp_prot_num);

        const struct protoent * pe = getprotobyname("tcp");
        tcp_prot_num = pe ? pe->p_proto : 6;

        return(tcp_prot_num);
    }

    
    
    std::set<EventMethEpollEquivImpl*> EventMethEpollEquivImpl::emee_cptr_set_;
    std::mutex EventMethEpollEquivImpl::emee_cptr_set_mutex_;

    // findEmEventInAnInterestSet scans the interest_ set of all the
    // EventMethEpollEquiv looking for an EMEvent pointer that matches arg. If
    // one if found, that matching EmEvent pointer is returned and
    // *epoll_equiv_cptr_out is set to point to the EventMethEpollEquiv whose
    // interest_ was found to contain the match; otherwise, NULL is returned
    // Note: is static function.
    EmEvent * EventMethEpollEquivImpl::findEmEventInAnInterestSet(
                  void * arg, EventMethEpollEquivImpl * * epoll_equiv_cptr_out)
    {
        PS_TIMEDBG_START;
        
        if (!epoll_equiv_cptr_out)
        {
            PS_LOG_WARNING("epoll_equiv_cptr_out null");
            throw std::invalid_argument("epoll_equiv_cptr_out null");
        }
        *epoll_equiv_cptr_out = NULL;

        if (!arg)
        {
            PS_LOG_WARNING("arg null");
            throw std::invalid_argument("arg null");
        }

        EmEvent * em_event = NULL;

        GUARD_AND_DBG_LOG(emee_cptr_set_mutex_);

        for(std::set<EventMethEpollEquivImpl *>::iterator it =
                                                        emee_cptr_set_.begin();
            it != emee_cptr_set_.end(); it++)
        {
            EventMethEpollEquivImpl * epoll_equiv = *it;
            if (!epoll_equiv)
            {
                PS_LOG_WARNING("epoll_equiv null");
                throw std::runtime_error("epoll_equiv null");
            }

            Fd this_fd = epoll_equiv->findFdInInterest((Fd) arg);
            if (this_fd)
            {
                em_event = this_fd;
                *epoll_equiv_cptr_out = epoll_equiv;
                break;
            }
        }
        return(em_event);
    }

    #ifdef DEBUG
    int EventMethEpollEquivImpl::getEmEventCount()
        { return(em_event_count__); }
    
    int EventMethEpollEquivImpl::getLibeventEventCount()
        { return(libevent_event_count__); }
    
    int EventMethEpollEquivImpl::getEventMethEpollEquivCount()
        { return(event_meth_epoll_equiv_count__); }
    
    int EventMethEpollEquivImpl::getEventMethBaseCount()
        { return(event_meth_base_count__); }

    int EventMethEpollEquivImpl::getWaitThenGetAndEmptyReadyEvsCount()
        { return(wait_then_get_count__); }
    #endif
    
    EventMethEpollEquivImpl::EventMethEpollEquivImpl(int size) :
        event_meth_base_(std::make_unique<EventMethBase>()),
        int_mut_locked_by_get_ready_em_events_(false)
    { // size is a hint as to how many FDs to be monitored
        
        PS_TIMEDBG_START;

        if (size <= 0)
        {
            PS_LOG_WARNING("size non-positive");
            throw std::invalid_argument("size non-positive");
        }

        if (!event_meth_base_)
        {
            PS_LOG_WARNING("EventMethBase * null");
            throw std::runtime_error("EventMethBase * null");
        }

        GUARD_AND_DBG_LOG(emee_cptr_set_mutex_);

        emee_cptr_set_.insert(this);
        
        INC_DEBUG_CTR(event_meth_epoll_equiv);
    }

    EventMethEpollEquivImpl::~EventMethEpollEquivImpl()
    {
        DEC_DEBUG_CTR(event_meth_epoll_equiv);

        PS_TIMEDBG_START_THIS;

        // When emee_cptr_set_mutex_ is to be locked together with
        // interest_mutex_, emee_cptr_set_mutex_ must be locked first
        GUARD_AND_DBG_LOG(emee_cptr_set_mutex_);
        emee_cptr_set_.erase(this);

        GUARD_AND_DBG_LOG(interest_mutex_);
        for(std::set<Fd>::iterator it = interest_.begin();
            it != interest_.end(); it++)
        {
            Fd fd = *it;
            if (fd) // forget this EventMethEpollEquiv which is being destroyed
                fd->detachEventMethEpollEquiv(); 
        }
        interest_.clear();

        // If both interest_mutex_ and ready_mutex_ are to be locked, lock
        // interest FIRST
        GUARD_AND_DBG_LOG(ready_mutex_);
        for(std::set<Fd>::iterator it = ready_.begin();
            it != ready_.end(); it++)
        {
            Fd fd = *it;
            if (fd) // forget this EventMethEpollEquiv which is being destroyed
                fd->detachEventMethEpollEquiv(); 
        }
        ready_.clear();

        // exit libevent loop
        // 
        // Note we call event_base_loopbreak not event_base_loopexit; the later
        // exits the loop after a time out, while the former exits immediately
        event_meth_base_->eMBaseLoopbreak();

        event_meth_base_ = NULL;
    }

    // Returns emee if emee is in emee_cptr_set_, or NULL
    // otherwise. emee_cptr_set_mutex_ is locked inside the function.
    EventMethEpollEquivImpl * EventMethEpollEquivImpl::
          getEventMethEpollEquivImplFromEmeeSet(EventMethEpollEquivImpl * emee)
    {
        GUARD_AND_DBG_LOG(emee_cptr_set_mutex_);
        
        std::set<EventMethEpollEquivImpl *>::iterator it(
                                                    emee_cptr_set_.find(emee));
        if (it == emee_cptr_set_.end())
            return(NULL);
        return(emee);
    }

    int EventMethEpollEquivImpl::getEventBaseFeatures() 
    {
        PS_TIMEDBG_START;
        
        return(event_meth_base_->getEventBaseFeatures());
    }

    int EventMethEpollEquivImpl::toEvEvents(
                                      const Flags<Polling::NotifyOn>& interest)
    {
        PS_TIMEDBG_START;
        
        int events = 0;

        if (interest.hasFlag(Polling::NotifyOn::Read))
            events |= EV_READ;
        if (interest.hasFlag(Polling::NotifyOn::Write))
            events |= EV_WRITE;

        // Note - it appears that macOS libevent does NOT support early-close
        // (@Feb/2024, macOS Sonoma 14.3)
        // !!!! Likely need to implement early-close "manually" in eventmeth.cc
        if (event_meth_base_->getEventBaseFeatures() & EV_FEATURE_EARLY_CLOSE)
        {
            if (interest.hasFlag(Polling::NotifyOn::Hangup))
                events |= EV_CLOSED; // EPOLLHUP
            if (interest.hasFlag(Polling::NotifyOn::Shutdown))
                events |= EV_CLOSED; // EPOLLRDHUP
        }

        // Note - with epoll, EPOLLRDHUP is caused if *the*peer* has issued a
        // shutdown(SHUT_WR) (peer has closed for writing).  Whereas EPOLLHUP
        // is issued if *both*sides* have issued a shutdown(SHUT_WR).

        // Note - Not all backends support EV_CLOSED. EV_CLOSED allows
        // detection of close events without having to read all the pending
        // data from the connection

        return events;
    }

    Flags<Polling::NotifyOn> EventMethEpollEquivImpl::toNotifyOn(Fd fd)
    {
        PS_TIMEDBG_START;
        
        if (!fd)
        {
            PS_LOG_WARNING("fd is NULL");
            throw std::runtime_error("fd is NULL");
        }
        
        int evm_events = fd->getReadyFlags();
        
        Flags<Polling::NotifyOn> flags;

        // Note: There is no Polling::NotifyOn::Timeout
        if (evm_events & EVM_READ)
            flags.setFlag(Polling::NotifyOn::Read);
        if (evm_events & EVM_WRITE)
            flags.setFlag(Polling::NotifyOn::Write);
        if (evm_events & EVM_CLOSED)
            flags.setFlag(Polling::NotifyOn::Hangup);
        if (evm_events & EVM_SIGNAL)
        {
            // Since this is a signal event, it cannot be a FdEventFd or timer
            
            int actual_fd_num = fd->getActualFd();
            // Per libevent documentation, this is the signal number being
            // monitored by the libevent event.
            // Most, but not all, signals require shutdown. Do "man signal" to
            // see the list.

            switch(actual_fd_num)
            {
            case SIGURG: // urgent condition present on socket
                flags.setFlag(Polling::NotifyOn::Hangup);
                break;
                
            case SIGCONT: // continue after stop
            case SIGCHLD: // child status has changed
            case SIGIO:   // I/O is possible on a descriptor (see fcntl(2))
            case SIGWINCH:// Window size change
            #ifndef __linux__
            case SIGINFO: // status request from keyboard
                // May be undefined on Linux, or means power failure (SIGPWR)
            #endif
                // Above conditions should be ignored... we set no flag
                break;
                
            default:
                flags.setFlag(Polling::NotifyOn::Shutdown);
                break;
            }
            
        }

        return flags;
    }

    Fd EventMethEpollEquivImpl::findFdInInterest(Fd fd)
    {
        PS_TIMEDBG_START;

        GUARD_AND_DBG_LOG(interest_mutex_);
        std::set<Fd>::iterator it = interest_.find(fd);
        if (it == interest_.end())
            return(PS_FD_EMPTY);

        return(fd);
    }

    // ev_flags is one or more EV_* flags
    // cb_arg is the parm passed to our callback by libevent. All being well it
    // will be a pointer to an EmEvent
    void EventMethEpollEquivImpl::handleEventCallback(void * cb_arg,
                                     #ifndef DEBUG
                                     [[maybe_unused]]
                                     #endif
                                                      em_socket_t cb_actual_fd,
                                                      short ev_flags)
    {
        PS_TIMEDBG_START_SQUARE;

        GUARD_AND_DBG_LOG(interest_mutex_);

        // We check again that cb_arg is in interest_ to make sure em_event
        // hasn't been closed/deleted since we called
        // findEmEventInAnInterestSet in eventCallbackFn
        // 
        // Note: So long as em_event has not already been closed/deleted, it
        // can't be deleted now so long as we have interest_mutex_ locked
        EmEvent * em_event = ((EmEvent *)cb_arg);
        if (interest_.find(em_event) == interest_.end())
        {
            PS_LOG_DEBUG_ARGS("cb_arg %p is not in interest_ of EMEEI %p",
                              cb_arg, this);
            return;
        }

        #ifdef DEBUG
        // There is no actual-fd for EmEventFd or EmEventTmrFd
        em_socket_t em_events_actual_fd = -1;
        if (em_event->getEmEventType() == Pistache::EmEvReg)
            em_events_actual_fd = em_event->getActualFdPrv();

        if (cb_actual_fd != em_events_actual_fd)
        {
            PS_LOG_WARNING_ARGS("EmEvent %p actual-fd %d doesn't match "
                                "callback parameter %d",
                                em_event, em_events_actual_fd, cb_actual_fd);
            return;
        }
        #endif

        // handleEventCallback may update ev_flags and/or em_event 
        em_event->handleEventCallback(ev_flags); 

        addEventToReadyInterestAlrdyLocked(em_event, ev_flags);
    }
    
    
    void EventMethEpollEquivImpl::addEventToReadyInterestAlrdyLocked(Fd fd,
                                                                short ev_flags)
    {
        PS_TIMEDBG_START_ARGS("EmEvent %p", fd);

        if (!fd)
        {
            PS_LOG_WARNING("fd null");
            throw std::runtime_error("fd null");
        }

        auto interest_it = interest_.find(fd);
        if (interest_it == interest_.end())
        {
            PS_LOG_DEBUG_ARGS("EmEvent %p of EMEEI %p no longer in interest_",
                              fd, this);

            // This could happen if fd was removed the EMEEI's interest_ during
            // a closeEvent. In that case, we do NOT want to add fd to ready_,
            // since fd may be closing or already closed, and will shortly be,
            // or has already been, deleted
            return;
        }
        
        GUARD_AND_DBG_LOG(ready_mutex_);

        #ifdef DEBUG
        short old_ev_flags = fd->getReadyFlags();
        #endif

        fd->orIntoReadyFlags(ev_flags);

        std::pair<std::set<Fd>::iterator, bool> ins_res = ready_.insert(fd);
        if (!ins_res.second)
        {
            PS_LOG_DEBUG_ARGS("EmEvent %p failed to insert in ready_, "
                              "ready flags were %s already",
                              fd,
                              old_ev_flags ? "set" : "not set");
            // This happens most commonly when a timer event expired, causing
            // the expiry count of the event to increment from zero to one,
            // which makes the timer event readable - so the event was already
            // in ready_ (because of the timeout), here we are adding EVM_READ
            // to the existing EVM_TIMEOUT for available (ready) events in the
            // event flags
        }
    }

    #ifdef DEBUG
    class WaitThenGetCountHelper
    {
    public:
        WaitThenGetCountHelper() {INC_DEBUG_CTR(wait_then_get);}
        ~WaitThenGetCountHelper() {DEC_DEBUG_CTR(wait_then_get);}
    };
    #endif

    // For use by the caller of getReadyEmEvents after getReadyEmEvents returns
    void EventMethEpollEquivImpl::unlockInterestMutexIfLocked()
    {
        GUARD_AND_DBG_LOG(int_mut_locked_by_get_ready_em_events_mutex_);
        if (int_mut_locked_by_get_ready_em_events_)
        {
            int_mut_locked_by_get_ready_em_events_ = false;
            PS_LOG_DEBUG_ARGS("Unlocking interest_mutex_ (at %p)",
                              &interest_mutex_);
            interest_mutex_.unlock();
        }
    }

    // For use of getReadyEmEventsHelper only
    void EventMethEpollEquivImpl::lockInterestMutex()
    {
        GUARD_AND_DBG_LOG(int_mut_locked_by_get_ready_em_events_mutex_);
        
        #ifdef DEBUG
        if (int_mut_locked_by_get_ready_em_events_)
            PS_LOG_WARNING_ARGS("interest_mutex_ (at %p) already locked?",
                                &interest_mutex_);
        #endif

        int_mut_locked_by_get_ready_em_events_ = true;
        PS_LOG_DEBUG_ARGS("Locking interest_mutex_ (at %p)", &interest_mutex_);
        interest_mutex_.lock();
    }
    
    

    int EventMethEpollEquivImpl::getReadyEmEvents(int timeout,
                                           std::set<Fd> & ready_evm_events_out)
    {
        #ifdef DEBUG
        // Increments wait_then_get_count__ here, and then automatically
        // decrements it again when wait_then_get_count_helper goes out of
        // scope
        WaitThenGetCountHelper wait_then_get_count_helper;
        #endif
        
        PS_TIMEDBG_START;
        
        int num_ready_out = 0;

        // Note: It's possible in thoery (perhaps not in practice) for
        // getReadyEmEventsHelper to move some events to
        // ready_evm_events_out, but then to remove them again e.g. if they
        // have null EmEvent. In that case, getReadyEmEventsHelper
        // returns -1, and we try again
        do {
            num_ready_out = 
              getReadyEmEventsHelper(timeout, ready_evm_events_out);
        } while (num_ready_out < 0);
        
        return(num_ready_out);
    }

    #ifdef DEBUG
    void EventMethEpollEquivImpl::logPendingOrNot()
    {
        PS_TIMEDBG_START;

        GUARD_AND_DBG_LOG(interest_mutex_);

        PS_LOG_DEBUG_ARGS("%u EmEvents in EMEE %p interest_",
                          interest_.size(), this);

        unsigned int i = 0;
        for(std::set<Fd>::iterator it = interest_.begin();
            it != interest_.end(); it++, i++)
        {
            Fd fd = *it;
            
            if (fd)
            {
                std::string pends("");

                struct timeval tv;
                if (fd->eventPending(EVM_TIMEOUT, &tv))
                    pends += " timeout";
                if (fd->eventPending(EVM_READ, &tv))
                    pends += " read";
                if (fd->eventPending(EVM_WRITE, &tv))
                    pends += " write";
                if (fd->eventPending(EVM_SIGNAL, &tv))
                    pends += " signal";

                if (pends.empty())
                    pends += " none";

                std::string readys("");

                if (fd->eventReady(EVM_TIMEOUT))
                    readys += " timeout";
                if (fd->eventReady(EVM_READ))
                    readys += " read";
                if (fd->eventReady(EVM_WRITE))
                    readys += " write";
                if (fd->eventReady(EVM_SIGNAL))
                    readys += " signal";

                if (readys.empty())
                    readys += " none";

                int actual_fd = -1;
                if (fd->getEmEventType() == EmEvReg)
                    actual_fd = fd->getActualFdPrv();
                
                PS_LOG_DEBUG_ARGS("#%u EmEvent %p of EMEE %p pending, "
                                  "type %s, "
                                  "actual fd %d, pending events%s, "
                                  "ready events%s",
                                  i, fd, this,
                                  emEventTypeToStr(fd->getEmEventType()),
                                  actual_fd, pends.c_str(), readys.c_str());
            }
            else
            {
                PS_LOG_DEBUG_ARGS("#%u null fd of EMEE %p", i, this);
            }
        }
    }
    #endif

    // If found in ready_, returns 1
    // If not found in ready_ but found in interest_, returns 0
    // If found in neither, returns -1
    int EventMethEpollEquivImpl::removeSpecialTimerFromInterestAndReady(
        EmEvent * loop_timer_eme,
        std::size_t * remaining_ready_size_out_ptr)
    {
        GUARD_AND_DBG_LOG(interest_mutex_);
        GUARD_AND_DBG_LOG(ready_mutex_);

        if (!loop_timer_eme)
        {
            PS_LOG_DEBUG("Null loop_timer_eme");
            return(-1);
        }

        std::size_t ready_erase_res = ready_.erase(loop_timer_eme); // 0 or 1
        std::size_t interest_erase_res = interest_.erase(loop_timer_eme);

        if (remaining_ready_size_out_ptr)
            *remaining_ready_size_out_ptr = ready_.size();

        if (ready_erase_res)
            return(1);
        if (interest_erase_res)
            return(0);
        return(-1);
    }

            

    // Waits (if needed) until events are ready, then sets the _out set to be
    // equal to ready events, and empties the list of ready events
    // "timeout" is in milliseconds, or -1 means wait indefinitely
    // Returns number of ready events being returned; or 0 if timed-out without
    // an event becoming ready; or -1, with errno set, on error
    //
    // NOTE: Caller must call unlockInterestMutexIfLocked after
    // getReadyEmEvents has returned and after the caller has finished
    // processing any Fds in ready_evm_events_out. getReadyEmEvents returns
    // with the interest mutex locked (or it may be locked) to ensure that
    // another thread cannot close an Fd in the interest list, given that that
    // Fd may also be in returned ready_evm_events_out and could be invalidated
    // by the close before the caller could get to it.
    int EventMethEpollEquivImpl::getReadyEmEventsHelper(
                              int timeout, std::set<Fd> & ready_evm_events_out)
    {
        PS_TIMEDBG_START_ARGS("EMEE %p", this);
        
        ready_evm_events_out.clear();

        { // encapsulate for loop_timer_eme
             
            std::shared_ptr<EmEvent> loop_timer_eme(NULL);

            #ifdef DEBUG
            PS_LOG_DEBUG("Listing interest_ before wait(event_base_dispatch)");
            logPendingOrNot();
            #endif

            if (timeout > 0)
            {
                PS_LOG_DEBUG_ARGS("Wait for events, timeout %dms",
                                  timeout);

                // Note: loop_timer_eme is a regular event with a timeout, NOT
                // a EmEventTmrFd (in this case, we don't want the
                // timerfd_create capabilities that EmEventTmrFd provides)
                loop_timer_eme =
                    std::shared_ptr<EmEvent>(EmEvent::make_new(
                                       -1 /* No file desc */, 0 /* No flags */,
                                       F_SETFDL_NOTHING, F_SETFDL_NOTHING));
                if (!loop_timer_eme)
                {
                    PS_LOG_WARNING("loop_timer_eme is NULL");
                    throw std::runtime_error("loop_timer_eme is NULL");
                }

                std::chrono::milliseconds rel_time_in_ms(timeout);
                if (ctl(EvCtlAction::Add, loop_timer_eme.get(), 0 /* events */,
                        &rel_time_in_ms) != 0)
                {
                    PS_LOG_WARNING("Failed to add loop_timer_eme");
                    throw std::runtime_error("Failed to add loop_timer_eme");
                    // Note: For events flag above, we do not set EVM_PERSIST,
                    // so the timer will fire once at most
                }
            }
            else
            {
                PS_LOG_DEBUG("Wait for events, no timeout");
            }

            // event_base_loop with EVLOOP_ONCE: block until we have an active
            // event, then exit once all active events have had their callbacks
            // run.
            // 
            // We can also break out of the loop by calling
            // event_base_loopbreak e.g. from a callback, though right now we
            // don't do so
            int dispatch_res = event_base_loop(
                              getEventMethBase()->getEventBase(), EVLOOP_ONCE);

            if (dispatch_res < 0)
            {
                PS_LOG_DEBUG("event_base_dispatch error");
                return(dispatch_res);
            }
        
            if (dispatch_res == 1)
            {
                PS_LOG_DEBUG("No pending or active events");
                return(0);
            }
        
            PS_LOG_DEBUG("event_base dispatch/loopexit success");
            #ifdef DEBUG
            logPendingOrNot();
            #endif

            if (loop_timer_eme)
            {
                std::size_t remaining_ready_size = 0;
            
                int remove_loop_timer_res =
                    removeSpecialTimerFromInterestAndReady(
                                loop_timer_eme.get(), &remaining_ready_size);
                if ((remove_loop_timer_res == 1) &&
                    (remaining_ready_size == 0))
                    return(0); // the only ready event was the loop timeout
            }

            
            loop_timer_eme = NULL; // Not needed since about to go out of scope
                                   // anyway, but to be clear...
            
            // loop_timer_eme gets deleted here, causing it to be removed from
            // loop as needed
        }

        // We lock interest_mutex_ here *before* we grab the ready events from
        // ready_, to ensure that no event (Fd) we get from ready_ can be
        // closed (noting that close of an Fd requires the interest_mutex_)
        // until processing of said event/Fd has completed
        
        lockInterestMutex(); // interest_mutex_ shall remain locked on return
                             // from this function

        { // encapsulate ready_mutex_ lock
            GUARD_AND_DBG_LOG(ready_mutex_);
            
            if (ready_.empty())
            {
                PS_LOG_DEBUG("ready_ empty despite dispatch completion");

                return(0);
            }
        
            PS_LOG_DEBUG_ARGS("ready_ events ready. Number: %d",
                              ready_.size());
        
            ready_evm_events_out = std::move(ready_);
            ready_.clear(); // probably unneeded because using std::move, but
                            // just in case...
        }

        std::size_t ready_evm_events_out_initial_size =
                                                   ready_evm_events_out.size();

        PS_LOG_DEBUG_ARGS("ready_evm_events_out_initial_size = %d",
                          ready_evm_events_out_initial_size);
        
        if (ready_evm_events_out_initial_size)
        {
            bool repeat_for = false;

            do {
                repeat_for = false;
                for(std::set<Fd>::iterator it = ready_evm_events_out.begin();
                    it != ready_evm_events_out.end(); it++)
                {
                    Fd em_event(*it);
                    if (!em_event)
                    {
                        PS_LOG_WARNING("ready em_event is NULL");
                        
                        ready_evm_events_out.erase(it); // remove from ready
                    
                        repeat_for = true;
                        break; // because erase invalidates iteratator it
                    }

                    PS_LOG_DEBUG("Event not null");
                    
                    if (!(em_event->getFlags() & EVM_PERSIST))
                    { // remove from interest_
                      // 
                      // Note: A non-persistent event becomes non-pending (aka
                      // not active) as soon as it is triggered, so should be
                      // removed from interest_

                        PS_LOG_DEBUG_ARGS("%p not persistent, "
                                          "removing from interest", em_event);

                        std::set<Fd>::iterator interest_it =
                                                     interest_.find(em_event);
                        if (interest_it != interest_.end())
                        {
                            Fd interest_em_event(*interest_it);
                            if (em_event == interest_em_event)
                                interest_.erase(interest_it);
                            else
                                PS_LOG_DEBUG("em_event doesn't match");
                        }
                        else
                        {
                            PS_LOG_DEBUG("em_event not found");
                        }
                    }
                }
            } while(repeat_for);
        }

        // Side note: We don't need to disarm an event that has been triggered
        // provided EV_PERSIST flag NOT set. (epoll has a flag that is the
        // equivalent of the inverse of EV_PERSIST - EPOLLONESHOT)

        int res = 0;

        if ((ready_evm_events_out_initial_size > 0) &&
            (ready_evm_events_out.size() <= 0))
            res = -1;
        else
            res = (int)(ready_evm_events_out.size());

        PS_LOG_DEBUG_ARGS("Returning %d", res);
        
        return(res);
    }

    Fd EventMethEpollEquivImpl::em_event_new( // static method
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
                             int f_setfl_flags  // e.g. O_NONBLOCK
        )
    {
        return(EmEvent::make_new(actual_fd, flags, 
                                 f_setfd_flags, f_setfl_flags));
    }

    Fd EventMethEpollEquivImpl::em_timer_new(clockid_t clock_id,
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
                              EventMethEpollEquivImpl * emee/*may be NULL*/)
    {
        return(EmEventTmrFd::make_new(clock_id, f_setfd_flags, f_setfl_flags,
                                      emee));
    }

    // For "eventfd-style" descriptors
    // 
    // Note that FdEventFd does not have an "actual fd" that the caller can
    // access; the caller must use FdEventFd's member functions instead
    FdEventFd EventMethEpollEquivImpl::em_eventfd_new(unsigned int initval,
                                          int f_setfd_flags, // e.g. FD_CLOEXEC
                                          int f_setfl_flags) // e.g. O_NONBLOCK
    {
        return(EmEventFd::make_new(initval, f_setfd_flags, f_setfl_flags));
    }

    // Add to interest list
    // Returns 0 for success, on error -1 with errno set
    int EventMethEpollEquivImpl::ctl(EvCtlAction op, // add, mod, or del
                                 Fd em_event,
                                 short     events, // bitmask of EVM_... events
                                 std::chrono::milliseconds * timeval_cptr)
    {
        PS_TIMEDBG_START;

        int ctl_res = ctlEx(op, em_event, events, timeval_cptr,
                            false/*forceEmEventCtlOnly*/);

        if (ctl_res == 0)
        {
            // add_was_artificial_ may or not be set, but we make sure reset
            em_event->resetAddWasArtificial();
        }

        return(ctl_res);
    }

    // Returns 0 for success, on error -1 with errno set
    // Will add/remove from interest_ if appropriate
    int EventMethEpollEquivImpl::ctl(EvCtlAction op, // add, mod, or del
                   EventMethEpollEquivImpl * epoll_equiv,
                   Fd event, // libevent event
                   short     events, // bitmask per epoll_ctl (EVM_... events)
                   std::chrono::milliseconds * timeval_cptr)
    { // static version
        PS_TIMEDBG_START;
        
        if(!epoll_equiv)
        {
            PS_LOG_WARNING("epoll_equiv null");
            throw std::invalid_argument("epoll_equiv null");
        }
        
        return(epoll_equiv->ctl(op, epoll_equiv, event, events, timeval_cptr));
    }
    

    // Add to interest list
    // Returns 0 for success, on error -1 with errno set
    // If forceEmEventCtlOnly is true, it will not call the ctl() function
    // of classes (like EmEventFd) derived from EmEvent but only the ctl
    // function of EmEvent itself. For internal use only.
    int EventMethEpollEquivImpl::ctlEx(EvCtlAction op, // add, mod, or del
                                   Fd em_event,
                                   short events, // bitmask of EVM... events
                                   std::chrono::milliseconds * timeval_cptr,
                                   bool forceEmEventCtlOnly)
    {
        PS_TIMEDBG_START_ARGS("emee %p, EvCtlAction %s, em_event %p, "
                              "events %s, timeval %dms",
                              this,
                              ctlActionToStr(op)->c_str(), em_event,
                              evmFlagsToStdString(events)->c_str(),
                              timeval_cptr ? timeval_cptr->count() : -1);

        if (!em_event)
        {
            PS_LOG_WARNING("em_event null");
            errno = EINVAL;
            return(-1);
        }

        bool eme_found_in_interest = false;
        { // encapsulate l_guard(interest_mutex_)
            GUARD_AND_DBG_LOG(interest_mutex_);

            std::set<Fd>::iterator eme_interest_it(interest_.find(em_event));
            eme_found_in_interest = (eme_interest_it != interest_.end());
        
            if ((op == EvCtlAction::Add) && (eme_found_in_interest) &&
                (!(em_event->addWasArtificial())))
            {
                PS_LOG_WARNING_ARGS(
                    "em_event %p not added to EMEE %p interest_, "
                    "and em_event->ctl(EvCtlAction::Add...) not called; "
                    "em_event is already in interest_",
                    em_event, this);
            
                errno = EEXIST;
                return(-1);
            }
        }
        

        int ctl_res = (forceEmEventCtlOnly ?
                       em_event->EmEvent::ctl(op, this, events, timeval_cptr) :
                       em_event->ctl(op, this, events, timeval_cptr));

        if (ctl_res == 0)
        {
            GUARD_AND_DBG_LOG(interest_mutex_);
            
            switch(op)
            {
            case EvCtlAction::Add:
            {
                if (!eme_found_in_interest)
                { // else was inserted artificially previously
                    std::pair<std::set<Fd>::iterator, bool> ins_res =
                        interest_.insert(em_event);
                    if (!ins_res.second)
                    {
                        PS_LOG_DEBUG_ARGS(
                            "em_event %p failed insert to EMEE %p interest_",
                            em_event, this);
                        errno = EPERM;
                        ctl_res = -1;
                        break;
                    }
                }

                PS_LOG_DEBUG_ARGS("em_event %p added to interest_ of EMEE %p",
                                  em_event, this);
            }
            break;

            case EvCtlAction::Mod:
            {
                if (!eme_found_in_interest)
                {
                    std::pair<std::set<Fd>::iterator, bool> ins_res =
                                                    interest_.insert(em_event);
                    if (!ins_res.second)
                    {
                         PS_LOG_DEBUG_ARGS(
                             "em_event %p failed insert to EMEE %p interest_",
                             em_event, this);
                         errno = EPERM;
                         ctl_res = -1;
                         break;
                    }
                    PS_LOG_DEBUG_ARGS(
                        "em_event %p added to interest_ of EMEE %p",
                        em_event, this);
                }
                else
                {
                    // Note: Moding an existing event that has already been
                    // added is permitted provided EPOLLEXCLUSIVE is not set.
                    // Pistache doesn't use EPOLLEXCLUSIVE, and we don't
                    // support it at present
                    
                    PS_LOG_DEBUG_ARGS(
                        "em_event %p in interest_ for Mod of EMEE %p",
                        em_event, this);
                }
            }
            break;

            case EvCtlAction::Del:
            {
                if (eme_found_in_interest)
                    interest_.erase(em_event);

                PS_LOG_DEBUG_ARGS(
                    "em_event %p %serased from interest_ of EMEE %p",
                    em_event, eme_found_in_interest ? "" : "NOT ", this);

                GUARD_AND_DBG_LOG(ready_mutex_);
                #ifdef DEBUG
                std::size_t erase_res = // num elements erased - 0 or 1
                #endif
                    ready_.erase(em_event); // again, ignore erase result
                PS_LOG_DEBUG_ARGS(
                    "em_event %p %serased from ready_ of EMEE %p%s",
                    em_event, erase_res ? "" : "not ", this,
                    erase_res ? "" : " (not present in ready_)");
            }
            break;

            default:
                PS_LOG_WARNING_ARGS("em_event %p for EMEE %p invalid action",
                                    em_event, this);
                errno = EINVAL;
                ctl_res = -1;
                break;
            }
        }

        PS_LOG_DEBUG_ARGS("ctl_res (int) = %d", ctl_res);
        return(ctl_res);
    }

     // rets 0 on success, -1 error
    int EventMethEpollEquivImpl::closeEvent(EmEvent * em_event)
    {
        PS_TIMEDBG_START;
        
        if (!em_event)
        {
            PS_LOG_INFO("em_event null");
            errno = EINVAL;
            return(-1);
        }

        EventMethEpollEquivImpl* emeei= em_event->getEventMethEpollEquivImpl();

        if (emeei)
        {
            GUARD_AND_DBG_LOG(emee_cptr_set_mutex_);
            // Note: We leave emee_cptr_set_mutex_ locked while we close the
            // emevent, erase it from interest_ and ready_, and delete it. The
            // prevents the EventMethEpollEquivImpl emeei itself exiting
            // (e.g. during a shutdown) while we are part way through the
            // close-erase-delete actions.

            // Check that emeei is in emee_cptr_set_. If it's not, the
            // EventMethEpollEquivImpl emeei may already be exiting, or exited,
            // and we don't want to access it
            if (emee_cptr_set_.find(emeei) != emee_cptr_set_.end())
            {
                std::mutex & int_mut(emeei->interest_mutex_);
                GUARD_AND_DBG_LOG(int_mut);

                // Check that the em_event is really, and still, in the emeei's
                // interest_ set.
                auto interest_it = emeei->interest_.find(em_event);
                if (interest_it != emeei->interest_.end())
                {
                    
                    std::mutex & red_mut(emeei->ready_mutex_);
            
                    GUARD_AND_DBG_LOG(red_mut);

                    int close_res = em_event->close();
                    if (close_res == 0)
                    {
                        // Erasing em_event from interest_ also stops em_event
                        // being added to ready_ subsequently (see
                        // addEventToReadyInterestAlrdyLocked), which is
                        // important given we're about to delete em_event
                        emeei->interest_.erase(interest_it);
                        #ifdef DEBUG
                        std::size_t res = 1 + emeei->ready_.erase(em_event);
                        PS_LOG_DEBUG_ARGS(
                            "Num erased from interest and ready: %u", res);
                        #else
                        emeei->ready_.erase(em_event);
                        #endif

                        // The interest and ready mutexes must remain locked
                        // while we perform the delete, to make sure we're not
                        // adding em_event to ready in another thread that's
                        // doing polling
                        // 
                        // Also so that if another thread is processing
                        // em_event (e.g. after polling) it can, by claiming
                        // interest_mutex_, avoid having em_event stamped on
                        // until it has finished processing it.
                        delete em_event;
                        DBG_DELETE_EMV(em_event);
                    }

                    #ifdef DEBUG
                    if (close_res != 0)
                        PS_LOG_DEBUG_ARGS(
                            "em_event->close() failed for %p", em_event);
                    #endif
            
                    return(close_res);
                }
            }
        }

        // No valid/safe EMEEI to erase from, just close and delete

        int close_res = em_event->close();
        if (close_res == 0)
        {
                delete em_event;
                DBG_DELETE_EMV(em_event);
        }
        #ifdef DEBUG
        else
        {
            PS_LOG_DEBUG_ARGS("em_event->close() failed for %p", em_event);
        }
        #endif
            
        return(close_res);
    }

    /* --------------------------------------------------------------------- */

    // Calls event_base_loopbreak for this base 
    int EventMethBase::eMBaseLoopbreak()
    {
        PS_TIMEDBG_START;
        
        return(event_base_loopbreak(getEventBase()));
    }

    // To enable to_string of an Fd
    std::string to_string(const EmEvent * eme)
                                {return(std::to_string((unsigned long) eme));}
    
/* ------------------------------------------------------------------------- */
    
    
} // of namespace Pistache

/* ------------------------------------------------------------------------- */

#endif // ifdef _USE_LIBEVENT

