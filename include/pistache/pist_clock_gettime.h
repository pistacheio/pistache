/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines a pist_clock_gettime when in an OS that does not provide
// clock_gettime natively, notably windows
//
// #include <pistache/pist_clock_gettime.h>

#ifndef _PIST_CLOCK_GETTIME_H_
#define _PIST_CLOCK_GETTIME_H_

#include <pistache/winornix.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

// PST_CLOCK_REALTIME, PST_CLOCK_MONOTONIC etc. defined in winornix.h

#include <time.h> // for time_t
#include <ctime> // for struct tm

/* ------------------------------------------------------------------------- */

extern "C" int PST_CLOCK_GETTIME(PST_CLOCK_ID_T clockid,
                                 struct PST_TIMESPEC *tp);

extern "C" struct tm *PST_GMTIME_R(const time_t *timep, struct tm *result);

extern "C" char *PST_ASCTIME_R(const struct tm *tm, char *buf);

extern "C" struct tm *PST_LOCALTIME_R(const time_t *timep, struct tm *result);

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS

/* ------------------------------------------------------------------------- */
#endif // of ifndef _PIST_CLOCK_GETTIME_H_
