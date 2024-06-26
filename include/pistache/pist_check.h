/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************
 * pist_check.h
 * 
 * Debugging breakpoints
 *
 */

#ifndef INCLUDED_PSCHECK_H
#define INCLUDED_PSCHECK_H

#include <mutex>

#include "pist_syslog.h"

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

#ifdef DEBUG

class GuardAndDbgLog // used by GUARD_AND_DBG_LOG below
{
public:
    GuardAndDbgLog(const char * mtx_name,
                   unsigned ln, const char * fn,
                   std::mutex * mutex_ptr);
    
    ~GuardAndDbgLog();

private:
    std::string mtx_name_;
    unsigned int locked_ln_;
    std::string locked_fn_;
    void * mutex_ptr_;
};

#endif // ifdef DEBUG

#ifdef DEBUG
#define GUARD_AND_DBG_LOG(_MTX_NAME_)                                   \
    GuardAndDbgLog guard_log_##_MTX_NAME_(PIST_QUOTE(_MTX_NAME_),     \
                                      __LINE__, __FILE__, &_MTX_NAME_); \
    PS_LOG_DEBUG_ARGS("Locking %s (at %p)", PIST_QUOTE(_MTX_NAME_),     \
                      &_MTX_NAME_);                                     \
    std::lock_guard<std::mutex> l_guard_##_MTX_NAME_(_MTX_NAME_);
#else
#define GUARD_AND_DBG_LOG(_MTX_NAME_)                                   \
    std::lock_guard<std::mutex> l_guard_##_MTX_NAME_(_MTX_NAME_);
#endif


// ---------------------------------------------------------------------------

#endif /* INCLUDED_PSCHECK_H */

// ---------------------------------------------------------------------------

