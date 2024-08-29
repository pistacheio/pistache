/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// #include "helpers/win_fork_ish.h"

#pragma once

#ifdef _WIN32 // defined for 32 or 64 bit Windows

/* ------------------------------------------------------------------------- */

typedef void * HANDLE; // as per WinNT.h

// Returns 0 if parent process, 1 if child process, and -1 on error in which
// case errno is set
int pist_simple_create_user_process(HANDLE * processHandle,
                                    HANDLE * threadHandle);

/* ------------------------------------------------------------------------- */

#endif // of ifdef _WIN32
