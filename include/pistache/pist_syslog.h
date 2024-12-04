/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************
 * pist_syslog.h
 *
 * Logging Facilities
 *
 * #include <pistache/pist_syslog.h>
 * 
 */


#ifndef INCLUDED_PS_LOG_H
#define INCLUDED_PS_LOG_H

/*****************************************************************************/

#include <ostream>

/*****************************************************************************/

#include <pistache/winornix.h>

#ifndef _IS_WINDOWS

#include <syslog.h>

#else // This is for Windows

// As per /usr/include/sys/syslog.h

#ifndef LOG_EMERG
#define LOG_EMERG       0       /* system is unusable */
#endif

#ifndef LOG_ALERT
#define LOG_ALERT       1       /* action must be taken immediately */
#endif

#ifndef LOG_CRIT
#define LOG_CRIT        2       /* critical conditions */
#endif

#ifndef LOG_ERR
#define LOG_ERR         3       /* error conditions */
#endif

#ifndef LOG_WARNING
#define LOG_WARNING     4       /* warning conditions */
#endif

#ifndef LOG_NOTICE
#define LOG_NOTICE      5       /* normal but significant condition */
#endif

#ifndef LOG_INFO
#define LOG_INFO        6       /* informational */
#endif

#ifndef LOG_DEBUG
#define LOG_DEBUG       7       /* debug-level messages */
#endif


#endif

/*****************************************************************************/
// Following macros do a log message with current file location. If you want to
// do a "printf" style parametrized log message, use the ..._ARGS version

#define PS_LOG_ALERT(__str) PS_LOG_ALERT_ARGS("%s", __str)
#define PS_LOG_ERR(__str) PS_LOG_ERR_ARGS("%s", __str)
#define PS_LOG_WARNING(__str) PS_LOG_WARNING_ARGS("%s", __str)
#define PS_LOG_INFO(__str) PS_LOG_INFO_ARGS("%s", __str)
#define PS_LOG_DEBUG(__str) PS_LOG_DEBUG_ARGS("%s", __str)

// If PS_LOG_AND_STDOUT is true, all logging is sent to stdout in addition to
// being sent to log file (for Windows, note the additional comment below).
//
// You can define PS_LOG_AND_STDOUT to true using the meson build option
// "PISTACHE_LOG_AND_STDOUT", or simply comment in the #define below.
//
// Note that, in the Windows case, sending of log messages to stdout is
// intended to be controlled principally not by the #define PS_LOG_AND_STDOUT
// but by the Registry key HKCU:\Software\pistacheio\pistache property
// psLogToStdoutAsWell; the Registry key property value can be set to 0 (off
// unless PS_LOG_AND_STDOUT is #defined to be true in which case on), 1 (on) or
// 10 (turn off even if PS_LOG_AND_STDOUT is #defined to be true). It defaults
// to 0 (i.e. off unless overridden by PS_LOG_AND_STDOUT). Any Registry key
// property value other than 0, 1 or 10 is treated like 1. Deleting the
// property or key is treated like 0. If the property value is changed while
// pistache.dll is running, the log output behavior will update dynamically.
// 
// #define PS_LOG_AND_STDOUT true

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

