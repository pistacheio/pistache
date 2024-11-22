/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines a ps_basename_r for OS that do not have basename_r natively
//
// #include <pistache/ps_basename.h>

#ifndef _PS_BASENAME_H_
#define _PS_BASENAME_H_

#include <pistache/winornix.h>

#include PIST_QUOTE(PST_MAXPATH_HDR) // for PST_MAXPATHLEN
/* ------------------------------------------------------------------------- */

#ifdef __APPLE__

/* ------------------------------------------------------------------------- */

#define PS_BASENAME_R basename_r
#include <libgen.h> // for basename_r

/* ------------------------------------------------------------------------- */

#else

/* ------------------------------------------------------------------------- */
// Linux, BSD or Windows

#define PS_BASENAME_R ps_basename_r

extern "C" char * ps_basename_r(const char * path, char * bname);

/* ------------------------------------------------------------------------- */

#endif // of ifdef __APPLE__... else...


/* ------------------------------------------------------------------------- */
#endif // of ifndef _PS_BASENAME_H_
