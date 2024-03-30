/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************
 * PistCheck.h
 * 
 * Debugging breakpoints
 *
 */

#ifndef INCLUDED_PSCHECK_H
#define INCLUDED_PSCHECK_H

#include "PistSysLog.h"

// If DEBUG is enabled, PS_LOGDBG_STACK_TRACE logs a stack trace
#ifdef DEBUG
#define PS_LOGDBG_STACK_TRACE                   \
    PS_LOG_WO_BREAK_UNLIMITED(LOG_DEBUG, "")
#else
#define PS_LOGDBG_STACK_TRACE
#endif

#define PS_LOGWRN(X)          \
    PS_LOG_WO_BREAK_LIMITED(LOG_WARNING, X, #X, 2048)
#define PS_LOGINF(X)          \
    PS_LOG_WO_BREAK_LIMITED(LOG_INFO, X, #X, 2048)
#define PS_LOGDBG(X)          \
    PS_LOG_WO_BREAK_LIMITED(LOG_DEBUG, X, #X, 2048)

#define PS_LOG_WO_BREAK_LIMITED(                                        \
    pri, condition, message, max_num)                                   \
    {                                                                   \
        if (!(condition))                                               \
        {                                                               \
            if (max_num)                                                \
            {                                                           \
                volatile static int checked_num = max_num;              \
                if (checked_num > 0)                                    \
                {                                                       \
                    checked_num--;                                      \
                    PS_LogWoBreak(pri, (message),                       \
                                  __FILE__, __LINE__, __FUNCTION__);    \
                }                                                       \
            }                                                           \
            else                                                        \
            {                                                           \
                PS_LogWoBreak(pri, (message),                           \
                              __FILE__, __LINE__, __FUNCTION__);        \
            }                                                           \
        }                                                               \
    }

#define PS_LOG_WO_BREAK_UNLIMITED(pri, message)                         \
    PS_LOG_WO_BREAK_LIMITED(pri, false /*condition*/,                   \
                            message,                                    \
                            0 /* max_num - no limit */)

// ---------------------------------------------------------------------------

extern int PS_LogWoBreak(int pri, const char *p,
                         const char *f, int l, const char * m = 0);

// ---------------------------------------------------------------------------

#endif /* INCLUDED_PSCHECK_H */

// ---------------------------------------------------------------------------

