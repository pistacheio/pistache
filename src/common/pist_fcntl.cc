/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines a pist_fcntl for Windows

#include <pistache/pist_fcntl.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

#include <stdarg.h>
#include <winsock.h>

#include <pistache/pist_timelog.h>
#include <pistache/pist_check.h> // for stack trace

/* ------------------------------------------------------------------------- */

/*
In Linux, only the CLOEXEC flag is supported for GET/SETFD
"In Linux, if the FD_CLOEXEC bit is set, the file descriptor will
automatically be closed during a successful execve(2). execve causes the
program that is currently being run by the calling process to be replaced
with a new program."
However, Windows has no execv/execve. It does have an _execv, but that just
calls CreateProcess, it doesn't replace the parent program.
So - FD_CLOEXEC is moot in Windows, and hence F_GETFD/SETFD is moot too
*/

static int fcntl_getfd([[maybe_unused]] em_socket_t fd)
{
    PS_TIMEDBG_START_ARGS("noop function, fd %d", fd);

    // Return  (as  the function result) the file descriptor flags
    return(0);
}

static int fcntl_setfd([[maybe_unused]] em_socket_t fd, int arg)
{
    PS_TIMEDBG_START_ARGS("fd %d, arg %d", fd, arg);

    if ((arg != 0) && (arg != PST_FD_CLOEXEC))
    {
        PS_LOG_WARNING_ARGS("Unsupported fcntl F_SETFD arg %d", arg);
        PS_LOGDBG_STACK_TRACE;
        errno = EINVAL;
        return(-1);
    }
    
    return(0); // success
}

/* ------------------------------------------------------------------------- */

static int fcntl_getfl([[maybe_unused]] em_socket_t fd)
{
    PS_TIMEDBG_START_ARGS("noop function, returns UNKNOWN, fd %d", fd);

    // Return (as the function result) the file access mode and the file status
    // flags
    return(PST_FCNTL_GETFL_UNKNOWN);
}

static int fcntl_setfl(em_socket_t fd, int arg)
{
    PS_TIMEDBG_START_ARGS("fd %d, arg %d", fd, arg);

    if ((arg != 0) && (arg != PST_O_NONBLOCK))
    {
        PS_LOG_WARNING_ARGS("Unsupported fcntl F_SETFL arg %d", arg);
        PS_LOGDBG_STACK_TRACE;
        errno = EINVAL;
        return(-1);
    }

    u_long opt = (arg == 0) ? 0 : 1;
    int ioc_res = ioctlsocket(fd, FIONBIO, &opt);
    if (ioc_res == 0)
        return(0); // success

    // Re: FIONBIO
    // https://learn.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls

    if (ioc_res != SOCKET_ERROR)
    {
        PS_LOG_WARNING_ARGS("Unexpected ioc_res %d", ioc_res);
    }
    else
    {
        int last_err = WSAGetLastError();
        PS_LOG_INFO_ARGS("ioctlsocket FIONBIO failed, ioc_res = SOCKET_ERROR, "
                         "WSAGetLastError %d", last_err);
    }
    PS_LOGDBG_STACK_TRACE;

    errno = EINVAL;
    return(-1);
}

/* ------------------------------------------------------------------------- */

extern "C" int PST_FCNTL(em_socket_t fd, int cmd, ... /* arg */ )
{
    int res = 0;

    va_list ptr;
    va_start(ptr, cmd);

    switch(cmd)
    {
    case PST_F_GETFD:
        res = fcntl_getfd(fd);
        break;

    case PST_F_SETFD:
    {
        int arg_val = va_arg(ptr, int);
        res = fcntl_setfd(fd, arg_val);
        break;
    }

    case PST_F_GETFL:
        res = fcntl_getfl(fd);
        break;

    case PST_F_SETFL:
    {
        int arg_val = va_arg(ptr, int);
        res = fcntl_setfl(fd, arg_val);
        break;
    }

    default:
        PS_LOG_WARNING_ARGS("Unsupported fcntl cmd %d", cmd);
        PS_LOGDBG_STACK_TRACE;

        errno = EINVAL;
        // Per Linux manpage, one meaning of EINVAL in fcntl is "The value
        // specified in cmd is not recognized by this kernel."
        res = -1;
    }

    va_end(ptr);
    return(res);
}



/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS
