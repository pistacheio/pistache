/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines to distinguish or abstract the differences between Windows and
// non-Windows OSes
//
// #include <pistache/winornix.h>

#ifndef _WINORNIX_H_
#define _WINORNIX_H_

#include <pistache/pist_quote.h>

/* ------------------------------------------------------------------------- */

// _WIN32 Defined for both 32-bit and 64-bit environments
// https://
//   learn.microsoft.com/en-us/windows-hardware/drivers/kernel/64-bit-compiler
// https://sourceforge.net/p/predef/wiki/OperatingSystems/

#if defined(_WIN16) || defined(_WIN32) || defined(_WIN64) ||            \
    defined(__WIN32__) || defined(__TOS_WIN__) || defined(__WINDOWS__)

#define _IS_WINDOWS 1

#endif

#ifdef _IS_WINDOWS
// Need to do this FIRST before including any windows headers, otherwise those
// headers may redefine min/max such that valid expressions like
// "std::numeric_limits<int>::max()" no longer work
#define NOMINMAX
#endif

#ifdef _IS_WINDOWS
#include <BaseTsd.h>
#include <cstdint>
#endif

#ifdef _IS_WINDOWS
#define PST_SSIZE_T SSIZE_T
#else
#define PST_SSIZE_T ssize_t
#endif

#ifdef _IS_WINDOWS
#define PST_RUSAGE_SELF     0
#define PST_RUSAGE_CHILDREN (-1)

#define PST_RUSAGE pst_rusage
#define PST_GETRUSAGE pist_getrusage

#else
#define PST_RUSAGE_SELF     RUSAGE_SELF
#define PST_RUSAGE_CHILDREN RUSAGE_CHILDREN

#define PST_RUSAGE rusage
#define PST_GETRUSAGE getrusage
#endif

// Use #include PIST_QUOTE(PST_SYS_RESOURCE_HDR)
#ifdef _IS_WINDOWS
#define PST_SYS_RESOURCE_HDR pistache/pist_resource.h
#else
#define PST_SYS_RESOURCE_HDR sys/resource.h
#endif

#ifdef _IS_WINDOWS
typedef int pst_clock_id_t;
#define PST_CLOCK_ID_T pst_clock_id_t

// see https://
//   learn.microsoft.com/en-us/windows/win32/api/winsock/ns-winsock-timeval
typedef long pst_suseconds_t;
#define PST_SUSECONDS_T pst_suseconds_t
typedef long pst_timeval_s_t;
#define PST_TIMEVAL_S_T pst_timeval_s_t

struct pst_timespec { long tv_sec; long tv_nsec; };
#define PST_TIMESPEC pst_timespec

#define PST_CLOCK_GETTIME pist_clock_gettime

#define PST_GMTIME_R pist_gmtime_r

#define PST_ASCTIME_R pist_asctime_r

#define PST_LOCALTIME_R pist_localtime_r

// Note: clock_gettime doesn't exist on Windows, we'll implement our own
// version. See pist_clock_gettime.h/.cc
#else
#define PST_CLOCK_ID_T clockid_t
#define PST_SUSECONDS_T suseconds_t
#define PST_TIMEVAL_S_T time_t
#define PST_TIMESPEC timespec
#define PST_CLOCK_GETTIME clock_gettime
#define PST_GMTIME_R gmtime_r
#define PST_ASCTIME_R asctime_r
#define PST_LOCALTIME_R localtime_r
#endif

#ifdef _IS_WINDOWS
typedef char PST_SOCK_OPT_VAL_T;
#else
typedef int PST_SOCK_OPT_VAL_T;
#endif


#ifdef _IS_WINDOWS
// As per /usr/include/linux/time.h

// If additional PST_CLOCK_... constants are defined (commented in), then they
// must be supported in the PST_CLOCK_GETTIME function implementation (see
// pist_clock_gettime.h/.cc).
#define PST_CLOCK_REALTIME                      0
#define PST_CLOCK_MONOTONIC                     1
#define PST_CLOCK_PROCESS_CPUTIME_ID            2
#define PST_CLOCK_THREAD_CPUTIME_ID             3
#define PST_CLOCK_MONOTONIC_RAW                 4
#define PST_CLOCK_REALTIME_COARSE               5
#define PST_CLOCK_MONOTONIC_COARSE              6
// #define PST_CLOCK_BOOTTIME                      7
// #define PST_CLOCK_REALTIME_ALARM                8
// #define PST_CLOCK_BOOTTIME_ALARM                9

#else
#define PST_CLOCK_REALTIME CLOCK_REALTIME
#define PST_CLOCK_MONOTONIC CLOCK_MONOTONIC
#define PST_CLOCK_PROCESS_CPUTIME_ID CLOCK_PROCESS_CPUTIME_ID
#define PST_CLOCK_THREAD_CPUTIME_ID CLOCK_THREAD_CPUTIME_ID
#define PST_CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC_RAW
#define PST_CLOCK_REALTIME_COARSE CLOCK_REALTIME_COARSE
#define PST_CLOCK_MONOTONIC_COARSE CLOCK_MONOTONIC_COARSE
// #define PST_CLOCK_BOOTTIME CLOCK_BOOTTIME
// #define PST_CLOCK_REALTIME_ALARM CLOCK_REALTIME_ALARM
// #define PST_CLOCK_BOOTTIME_ALARM CLOCK_BOOTTIME_ALARM
#endif

// Use #include PIST_QUOTE(PST_CLOCK_GETTIME_HDR)
#ifdef _IS_WINDOWS
#define PST_CLOCK_GETTIME_HDR pistache/pist_clock_gettime.h
#else
#define PST_CLOCK_GETTIME_HDR time.h
#endif

// Use #include PIST_QUOTE(PST_IFADDRS_HDR)
#ifdef _IS_WINDOWS
#define PST_IFADDRS_HDR pistache/pist_ifaddrs.h
#else
#define PST_IFADDRS_HDR ifaddrs.h
#endif

// Use #include PIST_QUOTE(PST_MAXPATH_HDR)
#ifdef _IS_WINDOWS
#define PST_MAXPATH_HDR Stdlib.h
#define PST_MAXPATHLEN _MAX_PATH
#else
#define PST_MAXPATH_HDR sys/param.h
#define PST_MAXPATHLEN MAXPATHLEN
#endif

// Use #include PIST_QUOTE(PST_STRERROR_R_HDR)
#if defined(__GNUC__) && !defined(__clang__)
#define PST_STRERROR_R_HDR string.h
#define PST_STRERROR_R strerror_r // returns char *
#else
#define PST_STRERROR_R_HDR pistache/pist_strerror_r.h
#define PST_STRERROR_R pist_strerror_r // returns char *
#endif

// Use #include PIST_QUOTE(PST_FCNTL_HDR)
#ifdef _IS_WINDOWS
#define PST_FCNTL_HDR pistache/pist_fcntl.h
#define PST_FCNTL pist_fcntl

// As per Linux /usr/include/x86_64-linux-gnu/bits/fcntl-linux.h
#define PST_F_GETFD		1	/* Get file descriptor flags.  */
#define PST_F_SETFD		2	/* Set file descriptor flags.  */
#define PST_F_GETFL		3	/* Get file status flags.  */
#define PST_F_SETFL		4	/* Set file status flags.  */

#else
#define PST_FCNTL_HDR fcntl.h
#define PST_FCNTL fcntl

#define PST_F_GETFD F_GETFD
#define PST_F_SETFD F_SETFD
#define PST_F_GETFL F_GETFL
#define PST_F_SETFL F_SETFL
#endif
// In Windows, we don't support doing F_GETFL; return this magic num instead
#define PST_FCNTL_GETFL_UNKNOWN                         \
    (((int)(((unsigned int)((int)-1))/2)) - (0xded - 97))


// Use #include PIST_QUOTE(PST_NETDB_HDR)
#ifdef _IS_WINDOWS
#define PST_NETDB_HDR ws2tcpip.h
#else
#define PST_NETDB_HDR netdb.h
#endif

// Use #include PIST_QUOTE(PST_SOCKET_HDR)
#ifdef _IS_WINDOWS
#define PST_SOCKET_HDR winsock2.h
#else
#define PST_SOCKET_HDR sys/socket.h
#endif

// Use #include PIST_QUOTE(PST_ARPA_INET_HDR)
#ifdef _IS_WINDOWS
#define PST_ARPA_INET_HDR winsock2.h
#else
#define PST_ARPA_INET_HDR arpa/inet.h
#endif

// Use #include PIST_QUOTE(PST_NETINET_IN_HDR)
#ifdef _IS_WINDOWS
#define PST_NETINET_IN_HDR ws2def.h
#else
#define PST_NETINET_IN_HDR netinet/in.h
#endif

// Use #include PIST_QUOTE(PST_NETINET_TCP_HDR)
#ifdef _IS_WINDOWS
#define PST_NETINET_TCP_HDR winsock2.h
#else
#define PST_NETINET_TCP_HDR netinet/tcp.h
#endif


// Use #include PIST_QUOTE(PST_IFADDRS_HDR)
#ifdef _IS_WINDOWS
#define PST_IFADDRS_HDR pistache/pist_ifaddrs.h
#define PST_IFADDRS pist_ifaddrs
#define PST_GETIFADDRS pist_getifaddrs
#define PST_FREEIFADDRS pist_freeifaddrs
#else
#define PST_IFADDRS_HDR ifaddrs.h
#define PST_IFADDRS ifaddrs
#define PST_GETIFADDRS getifaddrs
#define PST_FREEIFADDRS freeifaddrs
#endif

// Use #include PIST_QUOTE(PST_SYS_UN_HDR)
#ifdef _IS_WINDOWS
#define PST_SYS_UN_HDR afunix.h
#else
#define PST_SYS_UN_HDR sys/un.h
#endif


#ifdef _IS_WINDOWS
struct in_addr;
typedef struct in_addr PST_IN_ADDR_T;
// in_addr is defined in winsock2.h
// https://
//   learn.microsoft.com/en-us/windows/win32/api/winsock2/ns-winsock2-in_addr
// Note that it is a union - effectively a "u_long" that can be accessed as 4
// u_char, 2 u_short, or 1 u_long
#else
#define PST_IN_ADDR_T in_addr_t
#endif



// Use #include PIST_QUOTE(PST_THREAD_HDR)
#ifdef _IS_WINDOWS
// Note: processthreadsapi.h appears to require the prior inclusion of
// windows.h as well.
// So make sure to include PST_THREAD_HDR only in C/C++ files, not in a header
// file where it could end up including the massive windows.h all over the
// place.
#define PST_THREAD_HDR processthreadsapi.h
#else
#define PST_THREAD_HDR pthread.h
#endif

// Use #include PIST_QUOTE(PST_ERRNO_HDR)
#ifdef _IS_WINDOWS
#define PST_ERRNO_HDR errno.h
#else
#define PST_ERRNO_HDR sys/errno.h
#endif

// Use #include PIST_QUOTE(PST_MISC_IO_HDR)
#ifdef _IS_WINDOWS
#define PST_MISC_IO_HDR io.h
// For _close etc.
#else
#define PST_MISC_IO_HDR unistd.h
#endif

// Use #include PIST_QUOTE(PIST_FILEFNS_HDR)
#ifdef _IS_WINDOWS
#define PIST_FILEFNS_HDR pistache/pist_filefns.h
#else
// unistd.h defines pread
#define PIST_FILEFNS_HDR unistd.h
#endif

#ifdef _IS_WINDOWS
// See:
//   https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/close
#define PST_CLOSE ::_close
#define PST_OPEN pist_open
#define PST_READ ::_read
#define PST_WRITE ::_write
#define PST_PREAD pist_pread

typedef int pst_mode_t;
#define PST_MODE_T pst_mode_t
#else
#define PST_CLOSE ::close
#define PST_OPEN ::open
#define PST_READ ::read
#define PST_WRITE ::write
#define PST_PREAD ::pread

#define PST_MODE_T mode_t
#endif

// Open flags
// In Linux, see man(2) open
// In Windows, see https://
//   learn.microsoft.com/en-us/cpp/c-runtime-library/reference/open-wopen
#ifdef _IS_WINDOWS
// Following flags are defined in Linux
// If commented out, it means there is no equivalent flag in Windows (though
// sometimes a similar effect can be achieved in Windows by different means)
// Note: File constants are declared in fcntl.h in Windows
// (https://learn.microsoft.com/en-us/cpp/c-runtime-library/file-constants)
#define PST_O_RDONLY _O_RDONLY
#define PST_O_WRONLY _O_WRONLY
#define PST_O_RDWR _O_RDWR
#define PST_O_APPEND _O_APPEND
// #define PST_O_ASYNC _O_ASYNC
// #define PST_O_CLOEXEC _O_CLOEXEC
#define PST_O_CREAT _O_CREAT
// #define PST_O_DIRECT _O_DIRECT
// #define PST_O_DIRECTORY _O_DIRECTORY
// #define PST_O_DSYNC _O_DSYNC
#define PST_O_EXCL _O_EXCL
// #define PST_O_LARGEFILE _O_LARGEFILE
// #define PST_O_NOATIME _O_NOATIME
// #define PST_O_NOCTTY _O_NOCTTY
// #define PST_O_NOFOLLOW _O_NOFOLLOW
// #define PST_O_NONBLOCK _O_NONBLOCK
// #define PST_O_NDELAY _O_NDELAY // same as O_NONBLOCK
// #define PST_O_PATH _O_PATH
// #define PST_O_SYNC _O_SYNC
#define PST_O_TMPFILE _O_TEMPORARY
#define PST_O_TRUNC _O_TRUNC

// Defined in Windows, not Linux (though might be achieved with pmode):
//   _O_BINARY
//   _O_SHORT_LIVED
//   _O_NOINHERIT
//   _O_RANDOM
//   _O_SEQUENTIAL
//   _O_TEXT, _O_U16TEXT, _O_U8TEXT, _O_WTEXT

#else
#define PST_O_RDONLY O_RDONLY
#define PST_O_WRONLY O_WRONLY
#define PST_O_RDWR O_RDWR
#define PST_O_APPEND O_APPEND
// #define PST_O_ASYNC O_ASYNC
// #define PST_O_CLOEXEC O_CLOEXEC
#define PST_O_CREAT O_CREAT
// #define PST_O_DIRECT O_DIRECT
// #define PST_O_DIRECTORY O_DIRECTORY
// #define PST_O_DSYNC O_DSYNC
#define PST_O_EXCL O_EXCL
// #define PST_O_LARGEFILE O_LARGEFILE
// #define PST_O_NOATIME O_NOATIME
// #define PST_O_NOCTTY O_NOCTTY
// #define PST_O_NOFOLLOW O_NOFOLLOW
// #define PST_O_NONBLOCK O_NONBLOCK
// #define PST_O_NDELAY O_NDELAY // same as O_NONBLOCK
// #define PST_O_PATH O_PATH
// #define PST_O_SYNC O_SYNC
#define PST_O_TMPFILE O_TMPFILE
#define PST_O_TRUNC O_TRUNC
#endif

#ifdef _IS_WINDOWS
// See GetCurrentThreadId
// Note: The PST_THREAD_ID is the unique system-wide thread id. A Windows
// thread HANDLE can be derived from the id if needed by calling OpenThread
typedef uint32_t pst_thread_id; // DWORD := uint32_t 
#define PST_THREAD_ID pst_thread_id

#define PST_THREAD_ID_SELF GetCurrentThreadId
#else
#include <sys/types.h> // to define pthread_t even without PST_THREAD_HDR
#define PST_THREAD_ID pthread_t

#define PST_THREAD_ID_SELF pthread_self
#endif

#ifdef _IS_WINDOWS
#define PST_STRNCASECMP _strnicmp
#else
#define PST_STRNCASECMP strncasecmp
#endif

#ifdef _IS_WINDOWS
// Reference /usr/include/asm-generic/fcntl.h
// #define PST_O_ACCMODE  	00000003
// #define PST_O_RDONLY	00000000
// #define PST_O_WRONLY	00000001
// #define PST_O_RDWR		00000002
// #define PST_O_CREAT		00000100	/* not fcntl */
// #define PST_O_EXCL		00000200	/* not fcntl */
// #define PST_O_NOCTTY	00000400	/* not fcntl */
// #define PST_O_TRUNC		00001000	/* not fcntl */
// #define PST_O_APPEND	00002000
#define PST_O_NONBLOCK	00004000
// #define PST_O_DSYNC		00010000	/* used to be O_SYNC, see below */
// #define PST_FASYNC		00020000	/* fcntl, for BSD compatibility */
// #define PST_O_DIRECT	00040000	/* direct disk access hint */
// #define PST_O_LARGEFILE	00100000
// #define PST_O_DIRECTORY	00200000	/* must be a directory */
// #define PST_O_NOFOLLOW	00400000	/* don't follow links */
// #define PST_O_NOATIME	01000000
#define PST_O_CLOEXEC	02000000  /* set close_on_exec - does nothing in Win */
#else
#include <unistd.h> // for O_NONBLOCK
// #define PST_O_ACCMODE O_ACCMODE
// #define PST_O_RDONLY O_RDONLY
// #define PST_O_WRONLY O_WRONLY
// #define PST_O_RDWR O_RDWR
// #define PST_O_CREAT O_CREAT
// #define PST_O_EXCL O_EXCL
// #define PST_O_NOCTTY O_NOCTTY
// #define PST_O_TRUNC O_TRUNC
// #define PST_O_APPEND O_APPEND
#define PST_O_NONBLOCK O_NONBLOCK
// #define PST_O_DSYNC O_DSYNC
// #define PST_FASYNC FASYNC
// #define PST_O_DIRECT O_DIRECT
// #define PST_O_LARGEFILE O_LARGEFILE
// #define PST_O_DIRECTORY O_DIRECTORY
// #define PST_O_NOFOLLOW O_NOFOLLOW
// #define PST_O_NOATIME O_NOATIME
#define PST_O_CLOEXEC O_CLOEXEC
#endif

#ifdef _IS_WINDOWS
// As per include/asm-generic/fcntl.h in Linux
#define PST_FD_CLOEXEC      1
#else
#define PST_FD_CLOEXEC      FD_CLOEXEC
#endif

#ifdef __GNUC__
// GCC 4.8+, Clang, Intel and other compilers compatible with GCC
#define unreachable() __builtin_unreachable()
// [[noreturn]] inline __attribute__((always_inline)) void unreachable() {__builtin_unreachable();}
#elif defined(_MSC_VER) // MSVC
[[noreturn]] __forceinline void unreachable() {__assume(false);}
#else // ???
inline void unreachable() {}
#endif
/// Note: Could also use std::unreachable for C++23

/* ------------------------------------------------------------------------- */
#endif // of ifndef _WINORNIX_H_
