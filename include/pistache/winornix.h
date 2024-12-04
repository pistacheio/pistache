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

// DO NOT include emosandlibevdefs.h here
// emosandlibevdefs.h includes winornix.h, and depends on it

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
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

// defined in ws2tcpip.h; defined here to avoid need to include big header
// files (winsock2.h and ws2tcpip.h) in our headers just for this type
typedef int PST_SOCKLEN_T;
#else
typedef int PST_SOCK_OPT_VAL_T;
#define PST_SOCKLEN_T socklen_t
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
#elif defined __APPLE__
#define PST_MAXPATH_HDR sys/syslimits.h
#define PST_MAXPATHLEN PATH_MAX
#else
#define PST_MAXPATH_HDR sys/param.h
#define PST_MAXPATHLEN MAXPATHLEN
#endif

// Do this so the PST_DECL_SE_ERR_P_EXTRA / PST_STRERROR_R_ERRNO macros have
// PST_MAXPATHLEN fully defined
#include PIST_QUOTE(PST_MAXPATH_HDR)

// Use #include PIST_QUOTE(PST_STRERROR_R_HDR)
// mingw gcc doesn't define strerror_r (Oct/2024)
// gcc on macOS does define strerror_r, but the XSI version not the POSIX one
#if defined(__linux__) || (defined(__GNUC__) && (!defined(__MINGW32__)) && \
      (!defined(__clang__)) && (!defined(__NetBSD__)) && (!defined(__APPLE__)))
#define PST_STRERROR_R_HDR string.h
#define PST_STRERROR_R strerror_r // returns char *
#else
#define PST_STRERROR_R_HDR pistache/pist_strerror_r.h
#define PST_STRERROR_R pist_strerror_r // returns char *
#endif

// Convenience macros to declare se_err for PST_STRERROR_R/PST_STRERROR_R_ERRNO
#define PST_DECL_SE_ERR_P_EXTRA                \
    char se_err[PST_MAXPATHLEN+16]; se_err[0] = 0

#define PST_STRERROR_R_ERRNO                    \
    PST_STRERROR_R(errno, &se_err[0], PST_MAXPATHLEN)
#ifdef DEBUG
#define PST_DBG_DECL_SE_ERR_P_EXTRA PST_DECL_SE_ERR_P_EXTRA
#else
#define PST_DBG_DECL_SE_ERR_P_EXTRA
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
    (static_cast<int>((static_cast<unsigned int>((-1)/2))) - (0xded - 97))


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
#ifndef __linux__
// pistache/pst_errno.h prevents mingw gcc's bad macro substitution on errno
// Same issue with clang on macOS and gcc on OpenBSD
#define PST_ERRNO_HDR pistache/pst_errno.h
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

// Use #include PIST_QUOTE(PIST_POLL_HDR)
#ifdef _IS_WINDOWS
#define PIST_POLL_HDR pistache/pist_sockfns.h
#else
#define PIST_POLL_HDR poll.h
#endif

// Use #include PIST_QUOTE(PIST_SOCKFNS_HDR)
#ifdef _IS_WINDOWS
#define PIST_SOCKFNS_HDR pistache/pist_sockfns.h
#else
// unistd.h defines pread
#define PIST_SOCKFNS_HDR unistd.h // has close, read and write in Linux
#endif

// PST_SOCK_xxx macros are for sockets. For files, use PST_FILE_xxx
#ifdef _IS_WINDOWS

// PST_SOCK_STARTUP_CHECK must be invoked before any winsock2 function. It can
// be invoked as many times as you like, it does nothing after the first time
// it is invoked, and it is threadsafe. All the PST_SOCK_xxx functions call it
// themselves, so you don't need to invoke PST_SOCK_STARTUP_CHECK before
// calling PST_SOCK_SOCKET for instance. However, if code outside of the
// PST_SOCK_xxx functions is calling winsock, that code must invoke
// PST_SOCK_STARTUP_CHECK before making the winsock call (e.g. before calling
// getaddrinfo()).
//
// Returns 0 on success, or -1 on failure with errno set.
#define PST_SOCK_STARTUP_CHECK pist_sock_startup_check()

#define PST_SOCK_GETSOCKNAME pist_sock_getsockname
#define PST_SOCKLEN_T int

#define PST_SOCK_CLOSE pist_sock_close
// Note - Windows use "unsigned int" for count, whereas Linux uses size_t. In
// general we use size_t for count in Pistache, hence why we cast here
#define PST_SOCK_READ(__fd, __buf, __count)                     \
    pist_sock_read(__fd, __buf, static_cast<size_t>(__count))
#define PST_SOCK_WRITE(__fd, __buf, __count)                    \
    pist_sock_write(__fd, __buf, static_cast<size_t>(__count))
#define PST_SOCK_SOCKET pist_sock_socket
// Note - Windows uses "int" for socklen_t, whereas Linux uses size_t. In
// general we use size_t for addresses' lengths in Pistache (e.g. in struct
// ifaddr), hence why we cast here
#define PST_SOCK_BIND(__sockfd, __addr, __addrlen)      \
    pist_sock_bind(__sockfd, __addr, static_cast<PST_SOCKLEN_T>(__addrlen))
#define PST_SOCK_ACCEPT pist_sock_accept
#define PST_SOCK_CONNECT pist_sock_connect
#define PST_SOCK_LISTEN pist_sock_listen
#define PST_SOCK_SEND pist_sock_send
#define PST_SOCK_RECV pist_sock_recv

// PST_POLLFD, PST_POLLFD_T + PST_NFDS_T defined in pist_sockfns.h for Windows
#define PST_SOCK_POLL pist_sock_poll

#else
#define PST_SOCK_GETSOCKNAME ::getsockname
#define PST_SOCKLEN_T socklen_t

#define PST_SOCK_CLOSE ::close
#define PST_SOCK_READ ::read
#define PST_SOCK_WRITE ::write
#define PST_SOCK_SOCKET ::socket
#define PST_SOCK_BIND ::bind
#define PST_SOCK_ACCEPT ::accept
#define PST_SOCK_CONNECT ::connect
#define PST_SOCK_LISTEN ::listen
#define PST_SOCK_SEND ::send
#define PST_SOCK_RECV ::recv

#define PST_SOCK_POLL ::poll
#define PST_POLLFD pollfd // Note - this is a type, not a function
typedef struct PST_POLLFD PST_POLLFD_T;
#define PST_NFDS_T nfds_t

#define PST_SOCK_STARTUP_CHECK
#endif


// PST_FILE_CLOSE, PST_FILE_OPEN, PST_FILE_READ, PST_FILE_WRITE and
// PST_FILE_PREAD are for *files*
// For sockets, make sure to use PST_SOCK_xxx macros (above)
#ifdef _IS_WINDOWS
// See:
//   https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/close

#define PST_FILE_CLOSE ::_close
#define PST_FILE_OPEN pist_open

// Note - Windows use "unsigned int" for count, whereas Linux uses size_t. In
// general we use size_t for count in Pistache, hence why we cast here
#define PST_FILE_READ(__fd, __buf, __count)                  \
    ::_read(__fd, __buf, static_cast<unsigned>(__count))
#define PST_FILE_WRITE(__fd, __buf, __count)         \
    ::_write(__fd, __buf, static_cast<unsigned int>(__count))
#define PST_FILE_PREAD pist_pread

#define PST_UNLINK ::_unlink
#define PST_RMDIR ::_rmdir

typedef int pst_mode_t;
#define PST_FILE_MODE_T pst_mode_t
#else
#define PST_FILE_CLOSE ::close
#define PST_FILE_OPEN ::open
#define PST_FILE_READ ::read
#define PST_FILE_WRITE ::write
#define PST_FILE_PREAD ::pread
#define PST_UNLINK ::unlink
#define PST_RMDIR ::rmdir

#define PST_FILE_MODE_T mode_t
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
#define PST_STRCASECMP _stricmp
#else
#define PST_STRCASECMP strcasecmp
#endif

// Note: PS_STRLCPY, PS_STRLCAT, PS_STRNCPY_S and PS_ESTRUNCATE are defined in
// ps_strl.h.

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
