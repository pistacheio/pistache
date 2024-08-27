/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// #include <pistache/em_socket_t.h>

#ifndef _EM_SOCKET_T_H_
#define _EM_SOCKET_T_H_

#include <pistache/winornix.h>

/* ------------------------------------------------------------------------- */

// A type wide enough to hold the output of "socket()" or "accept()".  On
// Windows, this is an intptr_t; elsewhere, it is an int.
// Note: Mapped directly from evutil_socket_t in libevent's event2/util.h
#ifdef _IS_WINDOWS
#define em_socket_t intptr_t
#else
#define em_socket_t int
#endif

/* ------------------------------------------------------------------------- */

#endif // ifndef _EM_SOCKET_T_H_
