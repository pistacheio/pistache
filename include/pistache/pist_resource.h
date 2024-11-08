/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines features of <sys/resource.h>, notably struct rusage and getrusage()
//
// #include <pistache/pist_resource.h>

#ifndef _PIST_PIST_RESOURCE_H_
#define _PIST_PIST_RESOURCE_H_

#include <pistache/winornix.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

#include <winsock2.h> // for struct timeval

/* ------------------------------------------------------------------------- */

// Note PST_RUSAGE_SELF and PST_RUSAGE_CHILDREN defined in winornix.h

struct PST_RUSAGE
{
	struct timeval ru_utime;	/* user time used */
	struct timeval ru_stime;	/* system time used */
};

extern "C" int pist_getrusage(int who, struct PST_RUSAGE * rusage);

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS

/* ------------------------------------------------------------------------- */
#endif // of ifndef _PIST_PIST_RESOURCE_H_
