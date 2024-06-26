/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************
 * PistSysLog.h
 *
 * Logging Facilities
 *
 */


#ifndef INCLUDED_PS_LOG_H
#define INCLUDED_PS_LOG_H

/*****************************************************************************/

#include <syslog.h>

#include <ostream>

/*****************************************************************************/
// Following macros do a log message with current file location. If you want to
// do a "printf" style parametrized log message, use the ..._ARGS version

#define PS_LOG_ALERT(__str) PS_LOG_ALERT_ARGS("%s", __str)
#define PS_LOG_ERR(__str) PS_LOG_ERR_ARGS("%s", __str)
#define PS_LOG_WARNING(__str) PS_LOG_WARNING_ARGS("%s", __str)
#define PS_LOG_INFO(__str) PS_LOG_INFO_ARGS("%s", __str)
#define PS_LOG_DEBUG(__str) PS_LOG_DEBUG_ARGS("%s", __str)

// If PS_LOG_AND_STDOUT is true, all logging is sent to stdout in addition to
// being sent to log file
// 
// You can define PS_LOG_AND_STDOUT to true using the meson build option
// "PISTACHE_LOG_AND_STDOUT", or simple #define it here
#ifndef PS_LOG_AND_STDOUT
#define PS_LOG_AND_STDOUT false
#endif

#define PS_LOG_ALERT_ARGS(__fmt, ...)                                   \
    PSLogFn(LOG_ALERT, PS_LOG_AND_STDOUT, __FILE__, __LINE__, __FUNCTION__, \
            __fmt, __VA_ARGS__)

#define PS_LOG_ERR_ARGS(__fmt, ...)                                     \
    PSLogFn(LOG_ERR, PS_LOG_AND_STDOUT, __FILE__, __LINE__, __FUNCTION__, \
            __fmt, __VA_ARGS__)

#define PS_LOG_WARNING_ARGS(__fmt, ...)                                 \
    PSLogFn(LOG_WARNING, PS_LOG_AND_STDOUT, __FILE__, __LINE__, __FUNCTION__, \
            __fmt, __VA_ARGS__)

#define PS_LOG_INFO_ARGS(__fmt, ...)                                    \
    PSLogFn(LOG_INFO, PS_LOG_AND_STDOUT, __FILE__, __LINE__, __FUNCTION__, \
            __fmt, __VA_ARGS__)

#ifdef DEBUG
#define PS_LOG_DEBUG_ARGS(__fmt, ...)                                   \
    PSLogFn(LOG_DEBUG, PS_LOG_AND_STDOUT, __FILE__, __LINE__, __FUNCTION__, \
            __fmt, __VA_ARGS__)
#else
#define PS_LOG_DEBUG_ARGS(__fmt, ...) { }
#endif

#ifdef DEBUG
// Log source file name, line number, and name of function
#define PS_LOG_FNNAME PS_LOG_DEBUG("")
#else
#define PS_LOG_FNNAME
#endif


// ---------------------------------------------------------------------------

// f: file, l: line, m: method/function
extern "C" void PSLogFn(int _pri, bool _andPrintf,
                        const char * f, int l, const char * m,
                        const char * _format, ...);

// Log without file location - used in Check code
extern "C" void PSLogNoLocFn(int _pri, bool _andPrintf,
                             const char * _format, ...);

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
extern "C" void setPsLogCategory(const char * _category);

// ---------------------------------------------------------------------------

#ifdef __APPLE__
#define my_basename_r basename_r
#include <libgen.h> // for basename_r
#else
#define my_basename_r ps_basename_r
extern "C" char * ps_basename_r(const char * path, char * bname);
#endif

// ---------------------------------------------------------------------------

// You can use these PSLG_... just like std::cout, except they go to the log
// E.g.:
//   PSLG_DEBUG_OS << "fd_actual value: " << fd_actual;

// To be implemented
#define PSLG_DEBUG_OS psLogOss.debug
#define PSLG_INFO_OS psLogOss.info
#define PSLG_WARNING_OS psLogOss.warning
#define PSLG_ERROR_OS psLogOss.error
#define PSLG_ALERT_OS psLogOss.alert


class PSLogOss
{
public:
    std::ostream debug;
    std::ostream info;
    std::ostream warning;
    std::ostream error;
    std::ostream alert;

    
    PSLogOss();
};

// extern PSLogOss psLogOss;

    
    
    

/*****************************************************************************/

#endif // of ifndef INCLUDED_PS_LOG_H

/*****************************************************************************/

