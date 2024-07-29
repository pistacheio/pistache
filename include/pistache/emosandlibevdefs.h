/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// EventMeth Operating System and libevent defines

// Defines, or does not define, _USE_LIBEVENT, _USE_LIBEVENT_LIKE_APPLE and
// _IS_BSD
// 
// emosandlibevdefs.h

#ifndef _EMOSANDLIBEVDEFS_H_
#define _EMOSANDLIBEVDEFS_H_

/* ------------------------------------------------------------------------- */

#ifdef PISTACHE_FORCE_LIBEVENT

// Force libevent even for Linux
#define _USE_LIBEVENT 1

// _USE_LIBEVENT_LIKE_APPLE not only forces libevent, but even in Linux causes
// the code to be as similar as possible to the way it is for __APPLE__
// (e.g. wherever possible, even on Linux it uses solely OS calls that are
// also available on macOS)
//
// Can comment out if not wanted
#define _USE_LIBEVENT_LIKE_APPLE 1

#endif // ifdef PISTACHE_FORCE_LIBEVENT

#ifdef _USE_LIBEVENT_LIKE_APPLE
  #ifndef _USE_LIBEVENT
    #define _USE_LIBEVENT 1
  #endif
#endif

#ifdef __APPLE__
  #ifndef _USE_LIBEVENT
    #define _USE_LIBEVENT 1
  #endif
  #ifndef _USE_LIBEVENT_LIKE_APPLE
    #define _USE_LIBEVENT_LIKE_APPLE 1
  #endif
#elif defined(_WIN32) // Defined for both 32-bit and 64-bit environments
  #define _USE_LIBEVENT 1
#elif defined(__unix__) || !defined(__APPLE__) && defined(__MACH__)
    #include <sys/param.h>
    #if defined(BSD)
        // FreeBSD, NetBSD, OpenBSD, DragonFly BSD
        //
        // Note - FreeBSD may support epoll via FreeBSD's Linux emulation layer
        // (see https://wiki.freebsd.org/Linuxulator). We can check for FreeBSD
        // using "ifdef __FreeBSD__" and also check __FreeBSD_version (see
        // https://docs.freebsd.org/en/books/porters-handbook/versions/).
        // However, since FreeBSD's Linuxulator supports Linux binaries, a user
        // might perhaps just as well run the Linux version of Pistache if
        // using the Linuxulator. So we use libevent (which will likely use
        // kqueue on FreeBSD) even in the FreeBSD case.
        #ifndef _USE_LIBEVENT
            #define _USE_LIBEVENT 1
        #endif
        #ifndef _USE_LIBEVENT_LIKE_APPLE
            #define _USE_LIBEVENT_LIKE_APPLE 1
        #endif
        #ifndef _IS_BSD
            #define _IS_BSD 1
        #endif
    #endif
#endif

/* ------------------------------------------------------------------------- */

#endif // ifndef _EMOSANDLIBEVDEFS_H_
