/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines a pist_strerror_r for use in Windows
//
// #include <pistache/pist_strerror_r.h>

#ifndef _PIST_STRERROR_R_H_
#define _PIST_STRERROR_R_H_

#include <pistache/winornix.h>

/* ------------------------------------------------------------------------- */

#if !defined(__linux__) && ((!defined(__GNUC__)) || (defined(__MINGW32__)) \
      || (defined(__clang__)) || (defined(__NetBSD__)) || (defined(__APPLE__)))

/* ------------------------------------------------------------------------- */
// Note: We provide the GNU-specific/POSIX style (which returns char *), not
// the XSI-compliant definition (which returns int) even in the non-GNU case.

extern "C" char * pist_strerror_r(int errnum, char *buf, size_t buflen);

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS

/* ------------------------------------------------------------------------- */
#endif // of ifndef _PIST_STRERROR_R_H_
