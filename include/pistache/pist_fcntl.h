/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines a pist_fcntl for Windows
//
// #include <pistache/pist_fcntl.h>

#ifndef _PIST_FCNTL_H_
#define _PIST_FCNTL_H_

#include <pistache/winornix.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

#include <pistache/em_socket_t.h>

/* ------------------------------------------------------------------------- */

extern "C" int PST_FCNTL(em_socket_t fd, int cmd, ... /* arg */ );

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS

/* ------------------------------------------------------------------------- */
#endif // of ifndef _PIST_FCNTL_H_
