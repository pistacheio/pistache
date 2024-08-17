/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines a pist_strerror_r for use in Windows

#include <pistache/pist_strerror_r.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

#include <string.h>
#include <algorithm>

/* ------------------------------------------------------------------------- */

extern "C" int pist_strerror_r(int errnum, char *buf, size_t buflen)
{
    if ((!buf) || (buflen <= 1))
        return(-1);

    buf[0] = 0;

    errno_t res_strerror_s = strerror_s(buf, buflen, errnum);
    if (res_strerror_s != 0)
    {
        const char * dumb_err = "{unknown err - srterror}";
        if (res_strerror_s == EINVAL)
            dumb_err = "{invalid errnum - srterror}";
        else if (res_strerror_s == ERANGE)
            dumb_err = "{small buf - srterror}";

        size_t ncpy = std::min(buflen-1, strlen(dumb_err));
        strncpy(buf, dumb_err, buflen-1);
        buf[ncpy] = 0;
    }

    return(0);
}


/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS
