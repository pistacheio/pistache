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
#endif // of ifndef _PS_STRL_H_
