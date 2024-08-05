/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************
 * pist_timelog.h
 *
 * Utility to log start and end time of activities
 *
 * Do "#define PS_TIMINGS_DBG 1" before including file to activate timing debug
 *
 */

#ifndef INCLUDED_PS_TIMELOG_H
#define INCLUDED_PS_TIMELOG_H

#include <time.h> // for clock_gettime and asctime
#include <stdio.h> // snprintf
#include <stdarg.h> // for vsnprintf

#include <string.h> // strlen

#include <cxxabi.h> // for abi::__cxa_demangle

#include <map>
#include <mutex>


// ---------------------------------------------------------------------------

#include <pistache/emosandlibevdefs.h> // For _IS_BSD
#include <pistache/pist_syslog.h>

#ifdef _IS_BSD
#define PS_STRLCPY(__dest, __src, __n) strlcpy(__dest, __src, __n)
#define PS_STRLCAT(__dest, __src, __size) strlcat(__dest, __src, __size)
#elif defined(__linux__)
#define PS_STRLCPY(__dest, __src, __n) strncpy(__dest, __src, __n)
#define PS_STRLCAT(__dest, __src, __size) strncat(__dest, __src, __size)
#elif defined(__APPLE__)
#define PS_STRLCPY(__dest, __src, __n) strlcpy(__dest, __src, __n)
#define PS_STRLCAT(__dest, __src, __size) strlcat(__dest, __src, __size)
#else
#define PS_STRLCPY(__dest, __src, __n) strcpy(__dest, __src)
#define PS_STRLCAT(__dest, __src, __size) strcat(__dest, __src)
#endif

#ifdef DEBUG
#define PS_TIMINGS_DBG 1
#endif

// Pointy delimiters (<...>) are default, others can be used
// Delimiters are repeated to indicate nesting (eg <<...>>)
// Note, in note DEBUG case, all these PS_TIMEDBG_START_xxx macros evaluate to
// nothing due to the non-DEBUG definition of PS_TIMEDBG_START_W_DELIMIT_CH
#define PS_TIMEDBG_START PS_TIMEDBG_START_POINTY
#define PS_TIMEDBG_START_POINTY PS_TIMEDBG_START_W_DELIMIT_CH('<')
#define PS_TIMEDBG_START_ROUND PS_TIMEDBG_START_W_DELIMIT_CH('(')
#define PS_TIMEDBG_START_SQUARE PS_TIMEDBG_START_W_DELIMIT_CH('[')
#define PS_TIMEDBG_START_CURLY PS_TIMEDBG_START_W_DELIMIT_CH('{')
#define PS_TIMEDBG_START_QUOTE PS_TIMEDBG_START_W_DELIMIT_CH('`')
#define PS_TIMEDBG_START_SLASH PS_TIMEDBG_START_W_DELIMIT_CH('\\')


#ifdef PS_TIMINGS_DBG

#define PS_TIMEDBG_START_W_DELIMIT_CH(PSDBG_DELIMIT_CH)                 \
    __PS_TIMEDBG __ps_timedbg(PSDBG_DELIMIT_CH,                         \
                                  __FILE__, __LINE__, __FUNCTION__, NULL);

#define PS_TIMEDBG_START_ARGS(__fmt, ...)                               \
    char ps_timedbg_buff[2048];                                         \
                                                                        \
    const char * ps_timedbg_inf =__PS_TIMEDBG::getInf(                  \
     &(ps_timedbg_buff[0]),sizeof(ps_timedbg_buff),__fmt, __VA_ARGS__); \
    __PS_TIMEDBG                                                        \
    __ps_timedbg('<', __FILE__, __LINE__, __FUNCTION__, ps_timedbg_inf)

// Same as PS_TIMEDBG_START but logs class name and "this" value
#define PS_TIMEDBG_START_THIS                                           \
    int __ptst_dem_status = 0;                                          \
    char * __ptst_demangled = abi::__cxa_demangle(                      \
        typeid(*this).name(), NULL, NULL, &__ptst_dem_status);          \
                                                                        \
    char ps_timedbg_this_buff[2048];                                    \
    if ((__ptst_demangled) && (__ptst_dem_status == 0))                 \
        snprintf(&(ps_timedbg_this_buff[0]),                            \
                 sizeof(ps_timedbg_this_buff),                          \
                 "%s (this) %p", __ptst_demangled, (void *) this);      \
    else                                                                \
        snprintf(&(ps_timedbg_this_buff[0]),                            \
                 sizeof(ps_timedbg_this_buff),                          \
                 "this %p",  (void *) this);                            \
    if ((__ptst_demangled) && (__ptst_dem_status == 0))                 \
        free(__ptst_demangled);                                         \
    PS_TIMEDBG_START_ARGS("%s", &(ps_timedbg_this_buff[0]));

    
#define PS_TIMEDBG_START_STR(__str)                                     \
    PS_TIMEDBG_START_ARGS("%s", __str)

#define PS_TIMEDBG_GET_CTR                                              \
    (__ps_timedbg.getCounter())
    
// ---------------------------------------------------------------------------

class __PS_TIMEDBG
{
private:
    const char mMarkerChar;
    
    const char * mFileName;
    int mLineNum;
    const char * mFnName;
    
    struct timespec mPsTimedbg;
    struct timespec mPsTimeCPUdbg;

    static unsigned mUniCounter; // universal (static) counter
    unsigned mCounter; // individual counter for this __PS_TIMEDBG

    static std::map<pthread_t, unsigned> mThreadMap;
    static std::mutex mThreadMapMutex;

    

    unsigned getThreadNextDepth() // returns depth value after increment
        {
            std::lock_guard<std::mutex> l_guard(mThreadMapMutex);
            pthread_t pthread_id = pthread_self();
            
            std::map<pthread_t, unsigned>::iterator it =
                mThreadMap.find(pthread_id);
            if (it == mThreadMap.end())
            {
                std::pair<pthread_t, unsigned> pr(pthread_id, 1);
                mThreadMap.insert(pr);
                return(1);
            }

            return(++(it->second));
        }
    unsigned decrementThreadDepth() // returns depth value before decrement
        {
            std::lock_guard<std::mutex> l_guard(mThreadMapMutex);
            pthread_t pthread_id = pthread_self();
            
            std::map<pthread_t, unsigned>::iterator it =
                mThreadMap.find(pthread_id);

            unsigned old_depth = 1;
            if (it->second) // else something went wrong
                old_depth = ((it->second)--);

            if (old_depth <= 1)
                mThreadMap.erase(it); // arguably optional, but avoids any risk
                                      // of leaks
            return(old_depth);
        }

    void setMarkerChars(char * marker_chars, char the_marker,
                        unsigned call_depth)
        {
            if (call_depth > 20)
            {
                for(unsigned int i=0; i<10; i++)
                    marker_chars[i] = the_marker;
                PS_STRLCPY(&(marker_chars[10]), "...", 4);

                marker_chars += (10 + 3);
                for(unsigned int i=0; i<10; i++)
                    marker_chars[i] = the_marker;
                marker_chars[10 + 3 + 10] = 0;
            }
            else
            {
                for(unsigned int i=0; i<call_depth; i++)
                    marker_chars[i] = the_marker;
                marker_chars[call_depth] = 0;
            }
        }

    char reverseMarkerChar(char the_marker)
        {
            switch(the_marker)
            {
            case '<':
                return('>');

            case '(':
                return(')');

            case '[':
                return(']');

            case '{':
                return('}');

            case '`':
                return('\'');

            case '\\':
                return('/');

            default:
                return(the_marker);
            }
        }
    
                
    
    

public:
    static const char * getInf(char * _buff, unsigned _buffSize,
                               const char * _format, ...)
        {
            if (!_format)
                return(NULL);

            if ((!_buff) || (_buffSize < 6))
                return(NULL);

            unsigned int sizeof_buf = _buffSize - 6;
            char * buf_ptr = &(_buff[0]);
            
            va_list ap;
            va_start(ap, _format);
            
            // Temporarily disable -Wformat-nonliteral
            #ifdef __clang__
            #pragma clang diagnostic push
            #pragma clang diagnostic ignored "-Wformat-nonliteral"
            #endif
            int ln = vsnprintf(buf_ptr, sizeof_buf, _format, ap);
            #ifdef __clang__
            #pragma clang diagnostic pop
            #endif
            
            va_end(ap);

            if (ln >= ((int) sizeof_buf))
                PS_STRLCAT(buf_ptr, "...", 5);

            return(buf_ptr);
        }
    
public:
    __PS_TIMEDBG(char marker_ch,
                 const char * f, int l, const char * m, const char * _inf) :
        mMarkerChar(marker_ch),
        mFileName(f), mLineNum(l), mFnName(m), mCounter(++mUniCounter)
        {
            const char * ps_time_str = "No-Time";
            char pschbuff[40];

            int res = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &mPsTimeCPUdbg);
            if (res != 0)
                memset(&mPsTimeCPUdbg, 0, sizeof(mPsTimeCPUdbg));
            res = clock_gettime(CLOCK_REALTIME, &mPsTimedbg);
            if (res == 0)
            {
                struct tm pstm;
                if (gmtime_r(&mPsTimedbg.tv_sec, &pstm) != NULL)
                {
                    if (asctime_r(&pstm, pschbuff) != NULL)
                    {
                        size_t pschbuff_len = strlen(pschbuff);
                        while((pschbuff_len > 0) &&
                              ((pschbuff[pschbuff_len-1] == '\n') ||
                               (pschbuff[pschbuff_len-1] == '\r')))
                            pschbuff_len--;
                        snprintf(&(pschbuff[pschbuff_len]),
                                 sizeof(pschbuff) - (pschbuff_len + 1),
                                 ",%03ldms", (mPsTimedbg.tv_nsec / 1000000));
                        ps_time_str = &(pschbuff[0]);
                    }
                }
            }

            char marker_chars[32];
            unsigned call_depth = getThreadNextDepth();
            setMarkerChars(&(marker_chars[0]), mMarkerChar, call_depth);
            
            if (_inf)
                PSLogFn(LOG_DEBUG, PS_LOG_AND_STDOUT,
                        mFileName, mLineNum, mFnName,
                        "%sCtr:%u %s [%s]", &(marker_chars[0]),
                        mCounter, ps_time_str, _inf);
            else
                PSLogFn(LOG_DEBUG, PS_LOG_AND_STDOUT,
                        mFileName, mLineNum, mFnName,
                        "%sCtr:%u %s", &(marker_chars[0]),
                        mCounter, ps_time_str);
        }

    ~__PS_TIMEDBG()
        {
            const char * ps_time_str = "No-Time";
            const char * ps_diff_time_str = "No-Time";
            char pschbuff[40];
            char ps_diff_chbuff[40];
            struct timespec latest_ps_timedbg;
            struct timespec latest_ps_time_cpu_dbg;
            int res = clock_gettime(CLOCK_PROCESS_CPUTIME_ID,
                                    &latest_ps_time_cpu_dbg);
            if (res != 0)
                memset(&latest_ps_time_cpu_dbg, 0,
                       sizeof(latest_ps_time_cpu_dbg));
            res = clock_gettime(CLOCK_REALTIME, &latest_ps_timedbg);

            if (res == 0)
            {
                struct tm pstm;
                if (gmtime_r(&latest_ps_timedbg.tv_sec, &pstm) != NULL)
                {

                    if (asctime_r(&pstm, pschbuff) != NULL)
                    {
                        size_t pschbuff_len = strlen(pschbuff);
                        while((pschbuff_len > 0) &&
                              ((pschbuff[pschbuff_len-1] == '\n') ||
                               (pschbuff[pschbuff_len-1] == '\r')))
                            pschbuff_len--;
                        snprintf(&(pschbuff[pschbuff_len]),
                                 sizeof(pschbuff) - (pschbuff_len + 1),
                            ",%03ldms", (latest_ps_timedbg.tv_nsec / 1000000));
                        ps_time_str = &(pschbuff[0]);
                    }
                }

                int diff_sec =
                    (int)(latest_ps_timedbg.tv_sec - mPsTimedbg.tv_sec);
                if (diff_sec < 31536000)
                {
                    long diff_nsec =
                        latest_ps_timedbg.tv_nsec - mPsTimedbg.tv_nsec;

                    long diff_msec = (((long)diff_sec) *  1000) +
                                                         (diff_nsec / 1000000);

                    if ((diff_msec < 10) &&
                        ((mPsTimeCPUdbg.tv_nsec) || (mPsTimeCPUdbg.tv_sec)) &&
                        ((latest_ps_time_cpu_dbg.tv_nsec) ||
                                              (latest_ps_time_cpu_dbg.tv_sec)))
                    {
                        diff_sec = (int)(latest_ps_time_cpu_dbg.tv_sec -
                                                         mPsTimeCPUdbg.tv_sec);
                        diff_nsec = latest_ps_time_cpu_dbg.tv_nsec -
                                                         mPsTimeCPUdbg.tv_nsec;
                        long diff_usec = (((long)diff_sec) *  1000000) +
                                                            (diff_nsec / 1000);
                        diff_msec = diff_usec / 1000;
                        long diff_ms_thousandths = diff_usec % 1000;
                            
                        snprintf(&(ps_diff_chbuff[0]),
                             sizeof(ps_diff_chbuff) - 1,
                                 "%ld.%03ldms", diff_msec,diff_ms_thousandths);
                        
                    }
                    else
                    {
                        snprintf(&(ps_diff_chbuff[0]),
                                 sizeof(ps_diff_chbuff) - 1,
                                 "%ldms", diff_msec);
                    }
                    
                    ps_diff_time_str = &(ps_diff_chbuff[0]);
                }
            }

            char marker_chars[32];
            unsigned call_depth = decrementThreadDepth();
            setMarkerChars(&(marker_chars[0]), reverseMarkerChar(mMarkerChar),
                           call_depth);
            
            PSLogFn(LOG_DEBUG, PS_LOG_AND_STDOUT,
                mFileName, mLineNum, mFnName,
                "%s diff=%s ctr:%u%s", ps_time_str, ps_diff_time_str, mCounter,
                &(marker_chars[0]));
        }

    unsigned getCounter() {return(mCounter);}
};

// ---------------------------------------------------------------------------

#else // of ifdef PS_TIMINGS_DBG

#define PS_TIMEDBG_START_W_DELIMIT_CH(PSDBG_DELIMIT_CH)

#define PS_TIMEDBG_START_ARGS(__fmt, ...) 

#define PS_TIMEDBG_START_THIS

#endif

// ---------------------------------------------------------------------------

#endif // of ifndef INCLUDED_PS_TIMELOG_H

// ---------------------------------------------------------------------------

