/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines a pist_clock_gettime when in an OS that does not provide
// clock_gettime natively, notably windows

#include <pistache/pist_clock_gettime.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

#include <ctime>

#include <sysinfoapi.h> // for GetSystemTimeAsFileTime

#include <mutex>

/* ------------------------------------------------------------------------- */

static bool lTimeAdjustmentInited = false;
static std::mutex lTimeAdjustmentInitedMutex;
static ULONGLONG lInitialMsSinceSystemStart = 0ull;
static struct PST_TIMESPEC lInitialTimespec = {0};

// rets 0 on success, -1 with errno on fail
static int initInitialMonoValsIfNotInited() 
{
    if (lTimeAdjustmentInited)
        return(0);
    
    std::lock_guard<std::mutex> lock(lTimeAdjustmentInitedMutex);
    if (lTimeAdjustmentInited)
        return(0);

    __int64 wintime = 0;
    GetSystemTimeAsFileTime((FILETIME*)&wintime);
    // GetSystemTimeAsFileTime retrieves the current system date and time. The
    // information is in Coordinated Universal Time (UTC)
    // format. GetSystemTimeAsFileTime is void / cannot fail.

    wintime      -=116444736000000000i64;           //1jan1601 to 1jan1970
    lInitialTimespec.tv_sec  =wintime / 10000000i64;           //seconds
    lInitialTimespec.tv_nsec =wintime % 10000000i64 *100;      //nano-seconds

    lInitialMsSinceSystemStart = GetTickCount64();
    if ((lInitialMsSinceSystemStart == 0) ||
        (((long long)(lInitialMsSinceSystemStart)) < 0))
    {
        errno = EFAULT;
        return(-1);
    }

    lTimeAdjustmentInited = true;

    return 0;
}


/* ------------------------------------------------------------------------- */

extern "C" int PST_CLOCK_GETTIME(PST_CLOCK_ID_T clockid,
                                 struct PST_TIMESPEC *spec)
{
    if (!spec)
    {
        errno = EFAULT;
        return(-1);
    }
    memset(spec, 0, sizeof(*spec));

    switch(clockid)
    {
    case PST_CLOCK_MONOTONIC:
    case PST_CLOCK_MONOTONIC_RAW:
    case PST_CLOCK_MONOTONIC_COARSE:
    {
        int init_res = initInitialMonoValsIfNotInited();
        if (init_res != 0)
            return(init_res);

        ULONGLONG ms_since_mono_vals_inited = GetTickCount64();
        if (ms_since_mono_vals_inited < lInitialMsSinceSystemStart)
        { // time went backwards?
            errno = EFAULT;
            return(-1);
        }
        ms_since_mono_vals_inited -= lInitialMsSinceSystemStart;

        ULONGLONG whole_s_since_mono_vals_inited =
                                              (ms_since_mono_vals_inited/1000);
        ULONGLONG remainder_ms_since_mono_vals_inited =
                                              (ms_since_mono_vals_inited%1000);

        spec->tv_sec= lInitialTimespec.tv_sec + whole_s_since_mono_vals_inited;

        long new_tv_nsec = ((1000000l * remainder_ms_since_mono_vals_inited) +
                            lInitialTimespec.tv_nsec);
        if (new_tv_nsec >= 1000000000l)
        {
            new_tv_nsec -= 1000000000l;
            spec->tv_sec++;
        }
        spec->tv_nsec = new_tv_nsec;

        break;
    }

    case PST_CLOCK_PROCESS_CPUTIME_ID:
    { // !!!!!!!! Consider GetProcessTimes instead?
        std::clock_t sct_time = std::clock();
        if (sct_time == std::clock_t(-1))
        {
            errno = ENOTSUP;
            return(-1);
        }

        spec->tv_sec  = (sct_time / CLOCKS_PER_SEC);
        spec->tv_nsec = ((1000000.0 * sct_time) % CLOCKS_PER_SEC) * 1000i64;
        // Note: POSIX defines CLOCKS_PER_SEC as one million, regardless of the
        // actual precision of clock

        break;
    }

    case PST_CLOCK_THREAD_CPUTIME_ID:
    {
        __int64 win_creation_time = 0;
        __int64 win_exit_time = 0;
        __int64 win_kernel_time = 0;
        __int64 win_user_time = 0;

        BOOL res_gtt = GetThreadTimes(GetCurrentThread(),
                                      (FILETIME*)&win_creation_time,
                                      (FILETIME*)&win_exit_time,
                                      (FILETIME*)&win_kernel_time,
                                      (FILETIME*)&win_user_time);
        if (!res_gtt)
        { // Possibly because of access rights
          // The thread handle must have THREAD_QUERY_INFORMATION or
          // THREAD_QUERY_LIMITED_INFORMATION
            
            errno = ENOTSUP;
            return(-1);
        }

        __int64 wintime = win_kernel_time + win_user_time;
        
        wintime      -=116444736000000000i64;           //1jan1601 to 1jan1970
        spec->tv_sec  =wintime / 10000000i64;           //seconds
        spec->tv_nsec =wintime % 10000000i64 *100;      //nano-seconds
        
        break;
    }
    
    case PST_CLOCK_REALTIME:
    case PST_CLOCK_REALTIME_COARSE:
    {
        __int64 wintime = 0;
        GetSystemTimeAsFileTime((FILETIME*)&wintime);
        // GetSystemTimeAsFileTime retrieves the current system date and
        // time. The information is in Coordinated Universal Time (UTC)
        // format. GetSystemTimeAsFileTime is void.

        // There are other Get...Time... functions as well, see:
        //   https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/

        wintime      -=116444736000000000i64;           //1jan1601 to 1jan1970
        spec->tv_sec  =wintime / 10000000i64;           //seconds
        spec->tv_nsec =wintime % 10000000i64 *100;      //nano-seconds
        
        break;
    }

    default:
        #ifdef DEBUG
        throw std::runtime_error("Unimplemented clockid");
        #endif
    
        errno = ENOTSUP;
        return(-1);
    }

    return(0);
}
    
/* ------------------------------------------------------------------------- */

extern "C" struct tm *PST_GMTIME_R(const time_t *timep, struct tm *result)
{
    // See for instance https://en.cppreference.com/w/c/chrono/gmtime
    errno_t res = gmtime_s(result, timep);
    if (res == 0)
        return(result);

    errno = res;
    return(NULL);
}


/* ------------------------------------------------------------------------- */

extern "C" char *PST_ASCTIME_R(const struct tm *tm, char *buf)
{
    errno_t res = asctime_s(buf, 26/*per Linux asctime_r man page*/, tm);
    if (res == 0)
        return(buf);

    errno = EOVERFLOW;
    return(NULL);
}

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS
