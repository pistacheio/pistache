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

#include <dlfcn.h>
#include "pistache/pist_syslog.h"
#include "pistache/pist_check.h"

/*****************************************************************************/

static void logStackTrace(int pri)
{
    PSLogNoLocFn(pri, "%s", "PS Check failed. Stack trace follows...");

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
            PSLogNoLocFn(pri, "%s", "  ST- [Null Stack entry] ");
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
                    PSLogNoLocFn(pri, "  ST- %p:%p %s", info.dli_saddr,
                                 stack[i], realname);
                }
                else
                {
                    PSLogNoLocFn(pri, "  ST- %p %s", stack[i], realname);
                }
            }
            else
            {
                if (info.dli_sname)
                    PSLogNoLocFn(pri, "  ST- %s", info.dli_sname);
                else if (info.dli_fname)
                    PSLogNoLocFn(pri, "  ST- [Unknown addr] %p in %s",
                                 stack[i], info.dli_fname);
                else
                    PSLogNoLocFn(pri, 
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
            PSLogNoLocFn(pri, "  ST- [Unknown addr] %p", stack[i]);
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
