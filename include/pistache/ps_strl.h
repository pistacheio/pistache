/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines a ps_strlcpy and ps_strlcat for OS that do not have strlcpy/strlcat
// natively, including Windows and (some e.g. Ubuntu LTS 22.04) Linux
//
// #include <pistache/ps_strl.h>

#ifndef _PS_STRL_H_
#define _PS_STRL_H_

#include <pistache/winornix.h>

/* ------------------------------------------------------------------------- */

#if defined(_IS_WINDOWS) || defined(__linux__)

/* ------------------------------------------------------------------------- */

extern "C" size_t ps_strlcpy(char *dst, const char *src, size_t size);
extern "C" size_t ps_strlcat(char *dst, const char *src, size_t size);

#define PS_STRLCPY(__dest, __src, __n) ps_strlcpy(__dest, __src, __n)
#define PS_STRLCAT(__dest, __src, __size) ps_strlcat(__dest, __src, __size)

/* ------------------------------------------------------------------------- */

#else // of #if defined(_IS_WINDOWS) || defined(__linux__)

/* ------------------------------------------------------------------------- */

// Assuming this is for a _IS_BSD or __APPLE__ case
#define PS_STRLCPY(__dest, __src, __n) strlcpy(__dest, __src, __n)
#define PS_STRLCAT(__dest, __src, __size) strlcat(__dest, __src, __size)

/* ------------------------------------------------------------------------- */

#endif // of #if defined(_IS_WINDOWS) || defined(__linux__)... else...

/* ------------------------------------------------------------------------- */

// ps_strncpy_s returns 0 for success, -1 on failure with errno set. NB: This
// is different from C++ standard (Annex K) strncpy_s which returns an errno_t
// on failure; we diverge because errno_t is often not defined on non-Windows
// systems. If the copy would result in a truncation, errno is set to
// PS_ESTRUNCATE.
extern "C" int ps_strncpy_s(char *strDest, size_t numberOfElements,
                            const char *strSource, size_t count);

#define PS_STRNCPY_S ps_strncpy_s
#ifdef _IS_WINDOWS
#define PS_ESTRUNCATE STRUNCATE
#else
#define PS_ESTRUNCATE E2BIG
#endif

/* ------------------------------------------------------------------------- */
#endif // of ifndef _PS_STRL_H_
