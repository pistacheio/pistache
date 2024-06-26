/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************
 * PistSysLog.cpp
 *
 * Logging Facilities
 *
 */

#include <mutex>


#include <pistache/pist_quote.h>

#include <libgen.h> // for basename or basename_r

#include <stdio.h> // snprintf
#include <stdlib.h> // malloc
#include <time.h>
#include <string.h>

#include <libgen.h> // basename
#include <cctype> // std::ispunct
#include <algorithm> // std::remove_copy_if
#include <limits.h> // PATH_MAX

#ifdef __APPLE__
#include <mach-o/dyld.h> // _NSGetExecutablePath
#endif

#ifdef __APPLE__
#include <Availability.h>
  // Apple's unified logging system launched in macOS Sierra (macOS 10.12) and
  // iOS 10. https://developer.apple.com/documentation/os/logging
  // The unified logging system was successor to Apple System Logger, which
  // itself replaced the use of syslog
  //
  // If viewing log in console.app, from console.app menu bar can do Action ->
  // Include Info Messages and Action -> Include Debug Messages.
  // Note that you have to press "start" in console.app to make it start
  // capturing logs.
  // In Console.app search, s;com.github.pistacheio.pistache [Enter] to filter
  // on our pistache subsysem
  //
  // sudo log stream --style compact --debug --info --predicate 'subsystem="com.github.pistacheio.pistache"'
  //
  // From the command line, try:
  //   sudo log stream --style compact --debug --info --predicate 'subsystem="com.github.pistacheio.pistache"'
  // ("log show" looks at existing logs, "stream" streams new log messages)
  // For stream, you can pipe to a file of your choice to retain of course
  // "man log" for more info

  #ifdef __MAC_OS_X_VERSION_MIN_REQUIRED
    #if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1012
      #define PIST_USE_OS_LOG 1
    #endif
  #endif
#endif

#ifdef PIST_USE_OS_LOG
  #include <os/log.h>
#else
  #include <syslog.h>
#endif

#include <stdarg.h>
#include <string.h> // for strcat
#include <pthread.h> // for pthread_self (getting thread ID)

#include <sys/param.h> // for MAXPATHLEN


#include <sys/types.h> // for getpid()
#include <unistd.h>

#include "pistache/pist_syslog.h"
#include <memory> // for std::shared_ptr


/*****************************************************************************/

class PSLogging
{
public:
    PSLogging();
    ~PSLogging();

public:
    static std::shared_ptr<PSLogging> getPSLogging();

public:
    void log(int _priority, bool _andPrintf,
             const char * _format, va_list _ap);
    void log(int _priority, bool _andPrintf,
             const char * _str);
};

// For use of class PSLogging only; treat as private to PSLogging
static std::mutex lPSLoggingSingletonMutex;
static std::shared_ptr<PSLogging> lPSLoggingSingleton;

/*****************************************************************************/

std::shared_ptr<PSLogging> PSLogging::getPSLogging()
{
    if (!lPSLoggingSingleton)
    {
        std::lock_guard<std::mutex> lock(lPSLoggingSingletonMutex);
        if (!lPSLoggingSingleton)
            lPSLoggingSingleton = std::make_shared<PSLogging>();
    }
    return(lPSLoggingSingleton);
}

// ---------------------------------------------------------------------------

#ifdef PIST_USE_OS_LOG
static os_log_t pist_os_log_ref = OS_LOG_DEFAULT;
#endif

static const char * gLogEntryPrefix = "PSTCH";
static char gIdentBuff[PATH_MAX + 6] = {0};
static bool gSetPsLogCategoryCalledWithNull = false;

static bool my_is_punct(std::string::value_type & ch)
{ // There's probably a way to do with with std::function
    return((bool)(std::ispunct((int)ch)));
}


static std::string getLogIdent()
{
    char prog_path[PATH_MAX+6];
    prog_path[0] = 0;
    
    #ifdef __APPLE__
    uint32_t bufsize = PATH_MAX;
    if (_NSGetExecutablePath(&(prog_path[0]), &bufsize) != 0)
        return(std::string());        
    #else
    if (readlink("/proc/self/exe", &(prog_path[0]), PATH_MAX) == -1)
        return(std::string());   
    #endif

    if (!strlen(&(prog_path[0])))
        return(std::string());

    char * prog_name = basename(&(prog_path[0]));
    if ((!prog_name) || (!strlen(prog_name)))
        prog_name = &(prog_path[0]);

    if (strlen(prog_name) <= 5)
        return(std::string(prog_name));

    std::string prog_name_raw(prog_name);
    std::string prog_name_no_punct;
    std::remove_copy_if(prog_name_raw.begin(), prog_name_raw.end(),            
          std::back_inserter(prog_name_no_punct), my_is_punct);

    if ((prog_name_no_punct.size() >= 3) && (prog_name_no_punct.size() <= 5))
        return(prog_name_no_punct);

    std::string & prog_name_to_sample((prog_name_no_punct.size() > 5) ?
                                      prog_name_no_punct : prog_name_raw);

    // Take the middle 5 chars
    std::string res(prog_name_to_sample.substr(prog_name_to_sample.size()/2 -3,
                                               5));
    return(res);
}


    
    
    

PSLogging::PSLogging()
{
    if (!gIdentBuff[0])
    {
        std::string log_ident(getLogIdent());
        strcpy(&(gIdentBuff[0]), log_ident.empty() ?
                                          gLogEntryPrefix : log_ident.c_str());
    }
    
    #ifdef PIST_USE_OS_LOG

    // Instead of syslog, on apple should likely be using os_log (and possibly
    // os_log_create). Note BOTH "man 3 os_log" the (logging call) and "man 5
    // os_log" configuration.

    pist_os_log_ref = os_log_create("com.github.pistacheio.pistache",
                                    &(gIdentBuff[0]));
    #else
    
    if (!gSetPsLogCategoryCalledWithNull)
    {
        
        int log_opts = (LOG_NDELAY | LOG_PID);
        #ifdef DEBUG    
        log_opts |= LOG_CONS; // send to console if syslog not working
        // OR with LOG_PERROR to send every log message to stdout
        #endif

        openlog(&(gIdentBuff[0]), log_opts, LOG_USER);
    }
    
    #endif // of not ifdef PIST_USE_OS_LOG
}

PSLogging::~PSLogging()
{
    if (!gSetPsLogCategoryCalledWithNull)
        closelog();
}

// ---------------------------------------------------------------------------

// rets -1 for fail, 0 for OK
static int snprintProcessAndThread(char * _buff, size_t _buffSize)
{
    if (!_buff)
        return(-1);
    if (!_buffSize)
        return(-1);
    
    pthread_t pt = pthread_self(); // This function always succeeds
    
    unsigned char *ptc = (unsigned char*)(void*)(&pt);
    int buff_would_have_been_len = 0;
    _buff[0] = 0;

    // Skip leading 0s to make str shorter (but don't reduce to zero len)
    size_t size_pt = sizeof(pt);
    unsigned int ch_to_skip = 0;
    for(;(ch_to_skip+1) < size_pt; ch_to_skip++)
    {
        if (ptc[ch_to_skip])
            break;
    }
    ptc += ch_to_skip;
    size_pt -= ch_to_skip;

    // Skip trailing 0s to make str shorter (but don't reduce to zero len)
    for(; size_pt>1; size_pt--)
    {
        if (ptc[size_pt-1])
            break;
    }
    
    for (int i=(((int)size_pt)-1); i>=0; i--) // little endian, if it matters
    {
        buff_would_have_been_len =
            snprintf(_buff, _buffSize, "%02x", (unsigned)(ptc[i]));
        if (buff_would_have_been_len <= 0)
        {
            _buff[0] = 0;
            return(-1);
        }
        if (((unsigned int)buff_would_have_been_len) >= _buffSize)
            return(-1); // output truncated, see snprintf man page
        _buff += buff_would_have_been_len;
        _buffSize -= buff_would_have_been_len;
    }

    return(0);
}

#ifdef PIST_USE_OS_LOG

#define OS_LOG_BY_PRIORITY_FORMAT_ARG(POL_FORM, POL_ARG)                \
    switch(_priority)                                                   \
    {                                                                   \
    case LOG_EMERG:                                                     \
    case LOG_ALERT:                                                     \
    case LOG_CRIT:                                                      \
        os_log_fault(pist_os_log_ref, POL_FORM, POL_ARG);               \
        break;                                                          \
                                                                        \
    case LOG_ERR:                                                       \
        os_log_error(pist_os_log_ref, POL_FORM, POL_ARG);               \
        break;                                                          \
                                                                        \
    case LOG_WARNING:                                                   \
        os_log(pist_os_log_ref, POL_FORM, POL_ARG);                     \
        break;                                                          \
                                                                        \
    case LOG_NOTICE:                                                    \
    case LOG_INFO:                                                      \
        os_log_info(pist_os_log_ref, POL_FORM, POL_ARG);                \
        break;                                                          \
                                                                        \
    case LOG_DEBUG:                                                     \
        os_log_debug(pist_os_log_ref, POL_FORM, POL_ARG);               \
        break;                                                          \
                                                                        \
    default:                                                            \
        os_log_fault(pist_os_log_ref, "Bad log priority %d",_priority); \
        os_log_fault(pist_os_log_ref, POL_FORM, POL_ARG);               \
        break;                                                          \
    }

#define OS_LOG_BY_PRIORITY OS_LOG_BY_PRIORITY_FORMAT_ARG("%s", &(buff[0]))
#endif

// ---------------------------------------------------------------------------

static const char * levelCStr(int _pri)
{
    const char * res = NULL;
    switch(_pri)
    {
    case LOG_ALERT:
        res = "ALERT";
        break;
        
    case LOG_WARNING:
        res = "WRN";
        break;
        
    case LOG_INFO:
        res = "INF";
        break;
        
    case LOG_DEBUG:
        res = "DBG";
        break;

    case LOG_ERR:
        res = "ERR";
        break;
        
    default:
        res = "UNKNOWN";
        break;
    }

    return(res);
}

static int logToStdOutMaybeErr(int _priority, bool _andPrintf,
                               const char * _str)
{
    if (!_str)
        _str = "";
    
    char dAndT[256];
    dAndT[0] = 0;
    if ((_andPrintf)
        #ifndef DEBUG
        || (_priority >= LOG_WARNING)
        #endif
        )
    {
        strcpy(&(dAndT[0]), "<No Timestamp>");

        time_t t = time(NULL);
        if (t >= 0)
        {
            struct tm * tm_ptr = localtime(&t);
            if (tm_ptr)
            {
                snprintf(&(dAndT[0]), sizeof(dAndT)-9, "%d %02d:%02d:%02d",
                         tm_ptr->tm_mday,
                         tm_ptr->tm_hour, tm_ptr->tm_min, tm_ptr->tm_sec);
            }
        }
    }
    
    
    int print_res = 0;

    if (_andPrintf)
        print_res = fprintf(stdout, "%s %s %s\n",
                            &(dAndT[0]), levelCStr(_priority), _str);

    #ifndef DEBUG
    if (_priority >= LOG_WARNING)
    {
        int stderr_print_res = 
            fprintf(stderr, "%s %s: %s\n",
                    &(dAndT[0]), levelCStr(_priority), _str);
        if (!_andPrintf)
            print_res = stderr_print_res;
    }
    #endif

    return(print_res);
}


// ---------------------------------------------------------------------------

void PSLogging::log(int _priority, bool _andPrintf,
                    const char * _format, va_list _ap)
{
    char buff[2048];
    buff[0] = '(';
    if (snprintProcessAndThread(&(buff[1]),
                                sizeof(buff)-3-strlen(gLogEntryPrefix)) >= 0)
        strcat(&(buff[0]), " ");
    strcat(&(buff[0]), gLogEntryPrefix);
    strcat(&(buff[0]), ") ");

    char * remaining_buff = &(buff[0]) + strlen(buff);
    size_t remaining_buff_size = sizeof(buff) - strlen(buff);

    #ifdef __clang__
    // Temporarily disable -Wformat-nonliteral
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wformat-nonliteral"
    #endif
    int res = vsnprintf(remaining_buff, remaining_buff_size-2, _format, _ap);
    #ifdef __clang__
    #pragma clang diagnostic pop
    #endif

    if ((res < 0) && (_priority >= LOG_WARNING))
    {
        logToStdOutMaybeErr(LOG_ALERT, _andPrintf,
                            "vsnprintf failed for log in PSLogging::log\n");

        #ifdef __clang__
        // Temporarily disable -Wformat-nonliteral
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wformat-nonliteral"
        #endif
        #ifdef PIST_USE_OS_LOG
          if (_priority < LOG_CRIT)
            _priority = LOG_CRIT;

          // Note: We do two separate os_log calls here, in case _format is
          // messed up in a way that both stops vsnprintf from working above,
          // and will stop os_log from working when using _format as a string
          // here
          OS_LOG_BY_PRIORITY_FORMAT_ARG("%s", 
                                        "Unable to log, vsnprintf failed");
          if (_format)
              OS_LOG_BY_PRIORITY_FORMAT_ARG(
                  "Failing log vsnprintf format: %s", _format);

        #else
          vsyslog(_priority, _format, _ap);
        #endif
        #ifdef __clang__
        #pragma clang diagnostic pop
        #endif
    }
    else
    {
        #ifdef PIST_USE_OS_LOG
        OS_LOG_BY_PRIORITY;
        #else
        syslog(_priority, "%s", &(buff[0]));
        #endif

        logToStdOutMaybeErr(_priority, _andPrintf, &(buff[0]));
    }
}

void PSLogging::log(int _priority, bool _andPrintf, const char * _str)
{
    if (!_str)
        return;

    char buff[2048];
    buff[0] = '(';
    if (snprintProcessAndThread(&(buff[1]),
                                sizeof(buff)-3-strlen(gLogEntryPrefix)) >= 0)
        strcat(&(buff[0]), " ");
    strcat(&(buff[0]), gLogEntryPrefix);
    strcat(&(buff[0]), ") ");
    
    char * remaining_buff = &(buff[0]) + strlen(buff);
    size_t remaining_buff_size = sizeof(buff) - strlen(buff);
    
    int res = snprintf(remaining_buff, remaining_buff_size-2, "%s", _str);
    if ((res < 0) && (_priority >= LOG_WARNING))
    {
        logToStdOutMaybeErr(LOG_ALERT, _andPrintf,
                            "snprintf failed for log in PSLogging::log");
        
        #ifdef PIST_USE_OS_LOG
          OS_LOG_BY_PRIORITY_FORMAT_ARG("%s", _str);
        #else
          syslog(_priority, "%s", _str);
        #endif
          
        logToStdOutMaybeErr(_priority, _andPrintf, _str);
    }
    else
    {
        #ifdef PIST_USE_OS_LOG
        OS_LOG_BY_PRIORITY;
        #else
        syslog(_priority, "%s", &(buff[0]));
        #endif

        logToStdOutMaybeErr(_priority, _andPrintf, &(buff[0]));
    }
}

/*****************************************************************************/

static void PSLogPrv(int _priority, bool _andPrintf,
                     const char * _format, va_list _ap)
{
    #ifndef DEBUG
    if (_priority == LOG_DEBUG)
        return;
    #endif
    if (!_format)
        return;
    
    PSLogging::getPSLogging()->log(_priority, _andPrintf, _format, _ap);
}

static void PSLogStrPrv(int _priority, bool _andPrintf, const char * _str)
{
    #ifndef DEBUG
    if (_priority == LOG_DEBUG)
        return;
    #endif
    if (!_str)
        return;
    
    PSLogging::getPSLogging()->log(_priority, _andPrintf, _str);
}

// ---------------------------------------------------------------------------

extern "C" void PSLogNoLocFn(int _pri, bool _andPrintf,
                             const char * _format, ...)
{
    int tmp_errno = errno; // See note on preserving errno in PSLogFn

    if (!_format)
        return;
    va_list ap;
    va_start(ap, _format);
    PSLogPrv(_pri, _andPrintf, _format, ap);
    va_end(ap);

    errno = tmp_errno;
}

extern "C" void PSLogFn(int _pri, bool _andPrintf,
                        const char * f, int l, const char * m,
                        const char * _format, ...)
{
    if (!_format)
        return;

    // We preserve errno for this function since i) We don't want the act of
    // logging (e.g. an error) to alter errno, even if the logging fails; and
    // ii) Apple's os_log_xxx appears to set errno to zero even when successful
    // (and the macOS man page for os_log has example code saying "os_log does
    // not preserve errno").
    int tmp_errno = errno;

    char bname_buff[MAXPATHLEN+6];
    if ((f) && (f[0]))
    {
        char * new_f = my_basename_r(f, &(bname_buff[0]));
        if ((new_f) && (new_f[0]))
            f = new_f;
    }

    const unsigned int form_and_args_buf_size = 2048;
    char form_and_args_buf[form_and_args_buf_size + 6];
    form_and_args_buf[0] = 0;
    
    { // encapsulate
        va_list ap;
        va_start(ap, _format);
        #ifdef __clang__
        // Temporarily disable -Wformat-nonliteral
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wformat-nonliteral"
        #endif
        int pos = vsnprintf(&(form_and_args_buf[0]),
                            form_and_args_buf_size, _format, ap);
        #ifdef __clang__
        #pragma clang diagnostic pop
        #endif
        
        va_end(ap);
        if (pos >= (int) form_and_args_buf_size)
            strcat(&(form_and_args_buf[0]), "...");
    }

    const unsigned int sizeof_buf = 4096;
    unsigned int sizeof_buf_ex_form_and_args =
                                       sizeof_buf - form_and_args_buf_size - 6;
    char buf[sizeof_buf + 6];
    char * buf_ptr = &(buf[0]);
    buf_ptr[0] = 0;
    int ln = 0;
    if (f || m)
    {
        if (f && m)
            ln= snprintf(buf_ptr, sizeof_buf_ex_form_and_args,
                         "%s:%d in %s()",f,l,m);
        else if (f)
            ln = snprintf(buf_ptr, sizeof_buf_ex_form_and_args,
                          "%s:%d", f, l);
        else
            ln= snprintf(buf_ptr,sizeof_buf_ex_form_and_args,
                         "line %d in %s()",l, m);

        if (ln >= (int) sizeof_buf_ex_form_and_args)
            strcat(buf_ptr, "...");
    }

    if (form_and_args_buf[0])
    { // Not empty string
        strcat(buf_ptr, ": ");
        strcat(buf_ptr, &(form_and_args_buf[0]));
    }

    PSLogStrPrv(_pri, _andPrintf, buf);

    errno = tmp_errno;
}

// ---------------------------------------------------------------------------

// If using SysLog (i.e. on Linux), if setPsLogCategory is called with NULL or
// zero-length string then pistachio does not call openlog; and if
// setPsLogCategory is called with a non-empty string before pistachio logs
// anything then the _category string will be passed to openlog as the "ident"
// parm upon the first pistachio log; or if setPsLogCategory is not called,
// then pistachio will assign a 5-letter ident based on the executable name.
// 
// Note that if (and this is NOT RECOMMENDED - instead get the app to call
// openlog itself before anything is logged) setPsLogCategory is called with
// NULL or empty string, but then pistachio logs something before the
// application can call openlog on its own account, then syslog will
// effectively call openlog itself using the app executable name for the ident.
//
// If using Apple "unified logging" (aka "os_log"), if setPsLogCategory is
// called with a non-null and non-zero-length value, then that value is used as
// the os_log category. Otherwise, the first time something is logged,
// pistachio assigns its own 5-letter category name derived from the executable
// name
//
// In either case, calling setPsLogCategory is optional
extern "C" void setPsLogCategory(const char * _category)
{
    if ((!_category) || (!strlen(_category)))
    {
        gSetPsLogCategoryCalledWithNull = true;
        return;
    }

    if (strlen(_category) >= PATH_MAX)
        return;

    gSetPsLogCategoryCalledWithNull = false;
    strcpy(&(gIdentBuff[0]), _category);
}

// ---------------------------------------------------------------------------

#ifndef __APPLE__
static std::mutex ps_basename_r_mutex;
extern "C" char * ps_basename_r(const char * path, char * bname)
{
    if (!bname)
        return(NULL);
        
    bname[0] = 0;
    
    std::lock_guard<std::mutex> l_guard(ps_basename_r_mutex);

    char * path_copy = (char *) malloc((path ? strlen(path) : 0) + 6);
    strcpy(path_copy, path); // since basename may change path contents

    char * bname_res = basename(path_copy);

    if (bname_res)
       strcpy(&(bname[0]), bname_res);

    free(path_copy);
    return(bname);
}
#endif // ifndef __APPLE__


// ---------------------------------------------------------------------------

// PSLogOss psLogOss;

// Implementation strategy:
// https://stackoverflow.com/questions/13703823/a-custom-ostream
// Create a streambuf with it's own sync function, initiatize ostream with the
// streambuf
    
