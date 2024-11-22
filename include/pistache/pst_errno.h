/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Includes an appropriate errno.h.
//
// Note that we use this intermediate include, rather than just relying solely
// on defining PST_ERRNO_HDR to be 'errno.h' for Windows, because of mingw
// gcc's treatment of errno as a macro - 'include PIST_QUOTE(PST_ERRNO_HDR)',
// with PST_ERRNO_HDR defined as 'errno.h', can translate to "(*_errno()).h",
// which is not what we want.
//
// #include <pistache/pst_errno.h>

#ifndef _PST_ERRNO_H_
#define _PST_ERRNO_H_

#include <pistache/winornix.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS
#include <errno.h>
#else
#include <sys/errno.h>
#endif

/* ------------------------------------------------------------------------- */

#endif // of ifndef _PST_ERRNO_H_
