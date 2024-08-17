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

#ifdef _IS_WINDOWS

/* ------------------------------------------------------------------------- */

extern "C" int pist_strerror_r(int errnum, char *buf, size_t buflen);

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS

/* ------------------------------------------------------------------------- */
#endif // of ifndef _PIST_STRERROR_R_H_
