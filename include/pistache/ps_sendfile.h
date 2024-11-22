/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines a Linux-style ps_sendfile when in an OS that does not provide one
// natively (BSD) or with a different interface (Windows)
//
//
//
// #include <pistache/ps_sendfile.h>

#ifndef _PIST_PS_SENDFILE_H_
#define _PIST_PS_SENDFILE_H_

#include <pistache/winornix.h>
#include <pistache/emosandlibevdefs.h>

/* ------------------------------------------------------------------------- */

#if defined(_IS_WINDOWS) || defined(_IS_BSD)

#include <sys/types.h> // off_t, size_t
#include <pistache/em_socket_t.h>

/* ------------------------------------------------------------------------- */

// Linux style sendfile
extern "C" PST_SSIZE_T ps_sendfile(em_socket_t out_fd, int in_fd,
                                   off_t *offset, size_t count);

#define PS_SENDFILE ps_sendfile

/* ------------------------------------------------------------------------- */

#else // of if defined(_IS_WINDOWS) || defined(_IS_BSD)

/* ------------------------------------------------------------------------- */

// Linux or macOS

#ifdef __linux__
#include <sys/sendfile.h>
#elif defined __APPLE__
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#endif

#define PS_SENDFILE ::sendfile

#endif // of if defined(_IS_WINDOWS) || defined(_IS_BSD)... else...

/* ------------------------------------------------------------------------- */
#endif // of ifndef _PIST_PS_SENDFILE_H_
