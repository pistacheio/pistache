/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines certain file functions (operations on an 'int' file descriptor) that
// exist in macOS/Linux/BSD but which need to be defined in Windows
//
// #include <pistache/pist_filefns.h>

#ifndef _PIST_FILEFNS_H_
#define _PIST_FILEFNS_H_

#include <pistache/winornix.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

#include <sys/types.h> // off_t, size_t

/* ------------------------------------------------------------------------- */

extern "C" PST_SSIZE_T pist_pread(int fd, void *buf,
                                  size_t count, off_t offset);

int pist_open(const char *pathname, int flags);

int pist_open(const char *pathname, int flags, PST_FILE_MODE_T mode);

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS

/* ------------------------------------------------------------------------- */
#endif // of ifndef _PIST_FILEFNS_H_
