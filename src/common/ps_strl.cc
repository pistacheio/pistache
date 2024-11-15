/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines a ps_strlcpy and ps_strlcat for OS that do not have strlcpy/strlcat
// natively, including Windows and (some) Linux

#include <pistache/ps_strl.h>

/* ------------------------------------------------------------------------- */

#include <errno.h>
#include <string.h> // for memcpy
#include <cstring>
#include <algorithm> // for std::min

#if defined(_IS_WINDOWS) || defined(__linux__)

/* ------------------------------------------------------------------------- */

extern "C" size_t ps_strlcpy(char *dst, const char *src, size_t n)
{
    if ((!dst) || (!src) || (!n))
        return(0);

    if (n == 1)
    {
        dst[0] = 0;
        return(0);
    }

    size_t bytes_to_copy = (strlen(src)+1);
    if (bytes_to_copy > n)
    {
        bytes_to_copy = n-1;

        std::memcpy(dst, src, bytes_to_copy);
        dst[bytes_to_copy] = 0;
    }
    else
    {
        std::memcpy(dst, src, bytes_to_copy);
    }

    return(bytes_to_copy);
}

extern "C" size_t ps_strlcat(char *dst, const char *src, size_t n)
{
    if ((!dst) || (!src) || (n <= 1))
        return(0);

    const size_t dst_len= strlen(dst);
    if ((n+1) <= dst_len)
        return(dst_len); // no space to add onto dst

    const size_t len_added = ps_strlcpy(dst+dst_len, src, (n - dst_len));
    return(dst_len+len_added);
}


/* ------------------------------------------------------------------------- */

#endif // of #if defined(_IS_WINDOWS) || defined(__linux__)

/* ------------------------------------------------------------------------- */


/* ------------------------------------------------------------------------- */

// ps_strncpy_s returns 0 for success, -1 on failure with errno set. NB: This
// is different from C++ standard (Annex K) strncpy_s which returns an errno_t
// on failure; we diverge because errno_t is often not defined on non-Windows
// systems. If the copy would result in a truncation, errno is set to
// PS_ESTRUNCATE.
extern "C" int ps_strncpy_s(char *strDest, size_t numberOfElements,
                            const char *strSource, size_t count)
{
#ifdef _IS_WINDOWS
    errno_t win_strncpy_s_res = strncpy_s(strDest, numberOfElements,
                                          strSource, count);
    if (win_strncpy_s_res == 0)
        return(0); // success

    if (win_strncpy_s_res == STRUNCATE)
    {
        errno = PS_ESTRUNCATE;
        return(-1);
    }

    errno = win_strncpy_s_res;
    return(-1);
#else // i.e. NOT Windows
    if ((!strDest) || (!strSource))
    {
        errno = EINVAL;
        return(-1);
    }

    size_t non_null_bytes_to_copy = std::min(strlen(strSource), count);
    if (non_null_bytes_to_copy >= numberOfElements)
    {
        errno = PS_ESTRUNCATE;
        return(-1);
    }

    std::memcpy(strDest, strSource, non_null_bytes_to_copy);
    strDest[non_null_bytes_to_copy] = 0;
    return(0);

#endif // of #ifdef _IS_WINDOWS
}





/* ------------------------------------------------------------------------- */



/* ------------------------------------------------------------------------- */
