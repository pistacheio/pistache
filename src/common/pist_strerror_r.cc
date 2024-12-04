/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines a pist_strerror_r for use in Windows

#include <pistache/winornix.h>
#include <pistache/pist_strerror_r.h>

#include <string.h>
#include <algorithm>

#include PIST_QUOTE(PST_ERRNO_HDR)

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

#include <pistache/ps_strl.h>

/* ------------------------------------------------------------------------- */

// Note: We use the GNU-specific definition (which returns char *), not the
// XSI-compliant definition (which returns int) even in the non-GNU case.

// strerror_s in Windows is the XSI form (returns int)

static const char * const_bad_strerror_parms = "{Invalid strerror_r parms}";
static char bad_strerror_parms_buff[128+16];

extern "C" char * pist_strerror_r(int errnum, char *buf, size_t buflen)
{
    if ((!buf) || (buflen <= 1))
    {
        if (::strcmp(&(bad_strerror_parms_buff[0]), const_bad_strerror_parms))
            PS_STRLCPY(&(bad_strerror_parms_buff[0]),
                    const_bad_strerror_parms, 128);

        return(&(bad_strerror_parms_buff[0]));
    }

    buf[0] = 0;

    errno_t res_strerror_s = strerror_s(buf, buflen, errnum);
    if (res_strerror_s == 0)
    {
        if ((errnum >= 100) && ((::strncmp(&(buf[0]), "Unknown error",
                                           sizeof("Unknown error")) == 0) ||
                                (::strncmp(&(buf[0]), "Unknown Error",
                                           sizeof("Unknown Error")) == 0) ||
                                (::strncmp(&(buf[0]), "unknown error",
                                           sizeof("unknown error")) == 0)))
        {
            // In Windows Server 2019 with Visual Studio 2019, the debug
            // runtime generates a real error message ("address in use") for
            // EADDRINUSE, but the release runtime simply outputs "Unknown
            // error". The release runtime produces real error messages for
            // errno below 100, e.g. "Resource temporarily unavailable" for
            // EAGAIN. When "Unknown error" is produced, we'll generate our own
            // message as per below. Note also that the Microsoft documentation
            // states that errno values 100 (EADDRINUSE) and up are "supported
            // for compatibility with POSIX". Finally, with Windows 11, even
            // the release runtime manages to produce real error messages for
            // errno 100; so this is presumably an issue with older versions of
            // Windows such as 2019.
            //
            // See: https://learn.microsoft.com/en-us/cpp/c-runtime-library/errno-constants?view=msvc-170

            const char * str = nullptr;
            switch(errnum)
            {
            case EADDRINUSE:
                str = "Address in use";
                break;

            case EADDRNOTAVAIL:
                str = "Address not available";
                break;

            case EAFNOSUPPORT:
                str = "Address family not supported";
                break;

            case EALREADY:
                str = "Connection already in progress";
                break;

            case EBADMSG:
                str = "Bad message";
                break;

            case ECANCELED:
                str = "Operation canceled";
                break;

            case ECONNABORTED:
                str = "Connection aborted";
                break;

            case ECONNREFUSED:
                str = "Connection refused";
                break;

            case ECONNRESET:
                str = "Connection reset";
                break;

            case EDESTADDRREQ:
                str = "Destination address required";
                break;

            case EHOSTUNREACH:
                str = "Host unreachable";
                break;

            case EIDRM:
                str = "Identifier removed";
                break;

            case EINPROGRESS:
                str = "Operation in progress";
                break;

            case EISCONN:
                str = "Already connected";
                break;

            case ELOOP:
                str = "Too many symbolic link levels";
                break;

            case EMSGSIZE:
                str = "Message size";
                break;

            case ENETDOWN:
                str = "Network down";
                break;

            case ENETRESET:
                str = "Network reset";
                break;

            case ENETUNREACH:
                str = "Network unreachable";
                break;

            case ENOBUFS:
                str = "No buffer space";
                break;

            case ENODATA:
                str = "No message available";
                break;

            case ENOLINK:
                str = "No link";
                break;

            case ENOMSG:
                str = "No message";
                break;

            case ENOPROTOOPT:
                str = "No protocol option";
                break;

            case ENOSR:
                str = "No stream resources";
                break;

            case ENOSTR:
                str = "Not a stream";
                break;

            case ENOTCONN:
                str = "Not connected";
                break;

            case ENOTRECOVERABLE:
                str = "State not recoverable";
                break;

            case ENOTSOCK:
                str = "Not a socket";
                break;

            case ENOTSUP:
                str = "Not supported";
                break;

            case EOPNOTSUPP:
                str = "Operation not supported";
                break;

#ifndef __MINGW32__
            case EOTHER:
                str = "Other";
                break;
#endif

            case EOVERFLOW:
                str = "Value too large";
                break;

            case EOWNERDEAD:
                str = "Owner dead";
                break;

            case EPROTO:
                str = "Protocol error";
                break;

            case EPROTONOSUPPORT:
                str = "Protocol not supported";
                break;

            case EPROTOTYPE:
                str = "Wrong protocol type";
                break;

            case ETIME:
                str = "Stream timeout";
                break;

            case ETIMEDOUT:
                str = "Timed out";
                break;

            case ETXTBSY:
                str = "Text file busy";
                break;

            case EWOULDBLOCK:
                str = "Operation would block";
                break;

            default:
                break;
            }
            if (str)
                PS_STRLCPY(buf, str, buflen);
        }
    }
    else
    {
        const char * dumb_err = "{unknown err - srterror}";
        if (res_strerror_s == EINVAL)
            dumb_err = "{invalid errnum - srterror}";
        else if (res_strerror_s == ERANGE)
            dumb_err = "{small buf - srterror}";

        PS_STRLCPY(buf, dumb_err, buflen);
    }

    return(buf);
}

/* ------------------------------------------------------------------------- */

#elif !defined(__linux__) && ((!defined(__GNUC__)) || (defined(__MINGW32__)) \
      || (defined(__clang__)) || (defined(__NetBSD__)) || (defined(__APPLE__)))

#include <pistache/ps_strl.h>

/* ------------------------------------------------------------------------- */

// Note: We use the GNU-specific definition (which returns char *), not the
// XSI-compliant definition (which returns int) even in the non-GNU case.

// Here, we assume native strerror_r is the XSI form (returns int)

static const char * const_bad_strerror_parms = "{Invalid strerror_r parms}";
static char bad_strerror_parms_buff[128];

extern "C" char * pist_strerror_r(int errnum, char *buf, size_t buflen)
{
    if ((!buf) || (buflen <= 1))
    {
        if (strcmp(&(bad_strerror_parms_buff[0]), const_bad_strerror_parms))
            strcpy(&(bad_strerror_parms_buff[0]), const_bad_strerror_parms);

        return(&(bad_strerror_parms_buff[0]));
    }

    buf[0] = 0;

    // Since it's not GNUC, we assume native strerror_r is the XSI form
    // (returns int)
    int res_strerror_r = strerror_r(errnum, buf, buflen);
    if (res_strerror_r != 0)
    {
        const char * dumb_err = "{unknown err - srterror}";
        if (res_strerror_r == EINVAL)
            dumb_err = "{invalid errnum - srterror}";
        else if (res_strerror_r == ERANGE)
            dumb_err = "{small buf - srterror}";

        PS_STRLCPY(buf, dumb_err, buflen);
    }

    return(buf);
}

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS... elsif ! defined(__GNUC__)
