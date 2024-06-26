/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************
 * pist_check.cc
 * 
 * Debugging breakpoints
 *
 */
#include <string.h> // memset
#include <map>
#include <mutex>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <cxxabi.h>
#include <execinfo.h>

#include <limits.h> // PATH_MAX

#include <dlfcn.h>
#include "pistache/pist_syslog.h"
#include "pistache/pist_check.h"

/*****************************************************************************/

static void logStackTrace(int pri)
{
    PSLogNoLocFn(pri, PS_LOG_AND_STDOUT,
                 "%s", "PS Check failed. Stack trace follows...");

    // write the stack trace to the logging facility

    const int BT_SIZE = 30;
    void *stack[BT_SIZE];
    size_t size = backtrace(stack, BT_SIZE);

    Dl_info info;
    // Start from 1, not 0 since everyone knows we are in PS_Break()
    for (size_t i = 1; i < size; ++i) 
    {
        if (!(stack[i]))
        {
            PSLogNoLocFn(pri, PS_LOG_AND_STDOUT,
                         "%s", "  ST- [Null Stack entry] ");
            continue;
        }
        
        if (dladdr(stack[i], &info) != 0) 
        {
            int status = 0;
                
            char* realname = abi::__cxa_demangle(info.dli_sname, NULL,
                                                 NULL, &status);
            if (realname && status == 0)
            {
                if (info.dli_saddr)
                {
                    PSLogNoLocFn(pri, PS_LOG_AND_STDOUT,
                                 "  ST- %p:%p %s", info.dli_saddr,
                                 stack[i], realname);
                }
                else
                {
                    PSLogNoLocFn(pri, PS_LOG_AND_STDOUT,
                                 "  ST- %p %s", stack[i], realname);
                }
            }
            else
            {
                if (info.dli_sname)
                    PSLogNoLocFn(pri, PS_LOG_AND_STDOUT,
                                 "  ST- %s", info.dli_sname);
                else if (info.dli_fname)
                    PSLogNoLocFn(pri, PS_LOG_AND_STDOUT,
                                 "  ST- [Unknown addr] %p in %s",
                                 stack[i], info.dli_fname);
                else
                    PSLogNoLocFn(pri, PS_LOG_AND_STDOUT,
                                 "  ST- [Unknown addr] %p in unknown file",
                                 stack[i]);

                // If you're getting a lot of unknown addresses (with
                // info.dli_sname NULL) that you know are in your own code, you
                // probably need to force linker to "add all symbols, not only
                // used ones, to the dynamic symbol table", by using -rdynamic
                // or -export-dynamic for linker flags.
                // https://gcc.gnu.org/onlinedocs/gcc/Link-Options.html
                // http://bit.ly/1KvWpPs (stackoverflow)
            }

            // -1: A memory allocation failiure occurred.
            if (realname && status != -1)
                free(realname);
        } 
        else
        {
            PSLogNoLocFn(pri, PS_LOG_AND_STDOUT,
                         "  ST- [Unknown addr] %p", stack[i]);
        }
    }
}


int PS_LogWoBreak(int pri, const char *p,
                  const char *f, int l, const char *m /* = 0 */)
{
    char buf[1024];
    int ln = 0;
    const char * p_prequote_symbol = (p && (strlen(p))) ? "\"" : "";
    const char * p_postquote_symbol = (p && (strlen(p))) ? "\" @" : "";
    
    if (m)
        ln = snprintf(buf, sizeof(buf),
                      "PS_LogPt: %s%s%s %s:%d in %s()\n",
                      p_prequote_symbol, p, p_postquote_symbol,
                      f, l, m);
    else
        ln = snprintf(buf, sizeof(buf), "PS_LogPt: %s%s%s %s:%d\n",
                       p_prequote_symbol, p, p_postquote_symbol,
                      f, l);
    
    // Print it
    if (ln >= ((int)sizeof(buf)))
    {
        ln = sizeof(buf);
        buf[sizeof(buf)-2] = '\n';
        buf[sizeof(buf)-1] = 0;
    }

    logStackTrace(pri);
    
    if ((pri == LOG_EMERG) || (pri == LOG_ALERT) || (pri == LOG_CRIT) ||
        (pri == LOG_ERR))
    {
        fprintf(stderr, "%s", buf); // provide to stderr since "major" issue
    }

    return(1);
}

// ---------------------------------------------------------------------------


#ifdef DEBUG

// class GuardAndDbgLog is used by GUARD_AND_DBG_LOG when DEBUG defined

GuardAndDbgLog::GuardAndDbgLog(const char * mtx_name,
                   unsigned ln, const char * fn,
                   std::mutex * mutex_ptr) :
    mtx_name_(mtx_name), locked_ln_(ln), mutex_ptr_(mutex_ptr)
{
    char buff[PATH_MAX+6];
    buff[0] = 0;
    locked_fn_ = std::string(my_basename_r(fn, &(buff[0])));
}

GuardAndDbgLog::~GuardAndDbgLog()
{
    PS_LOG_DEBUG_ARGS("%s (at %p) unlocked, was locked %s:%u",
                      mtx_name_.c_str(), mutex_ptr_,
                      locked_fn_.c_str(),
                      locked_ln_);
}

#endif


