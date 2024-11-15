/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines a ps_basename_r for OS that do not have basename_r natively

#include <pistache/ps_basename.h>

/* ------------------------------------------------------------------------- */


#ifndef __APPLE__

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

#include <stdlib.h> // for _splitpath_s
#include <string.h> // strlen
#include <pistache/ps_strl.h> // PS_STRLCPY and PS_STRLCAT

#include <algorithm> // std::max
#include <vector>

/* ------------------------------------------------------------------------- */

extern "C" char * ps_basename_r(const char * path, char * bname)
{
    int tmp_errno = errno; // preserve errno

    if (!bname)
        return(nullptr);
    bname[0] = 0;

    if (!path)
        return(bname);
    size_t path_len = strlen(&(path[0]));
    if (!path_len)
        return(bname);

    char drive[24]; drive[0] = 0;

    const size_t mpl = std::max<size_t>(path_len+16, PST_MAXPATHLEN+16);

    std::vector<char> dirname_buff(mpl);
    dirname_buff[0] = 0;

    std::vector<char> fname_buff(mpl);
    fname_buff[0] = 0;

    std::vector<char> ext_buff(mpl);
    ext_buff[0] = 0;

    errno_t sp_res = _splitpath_s(path,
                                  &(drive[0]), sizeof(drive)-8,
                                  dirname_buff.data(), mpl-8,
                                  fname_buff.data(), mpl-8,
                                  ext_buff.data(), mpl-8);

    if (sp_res)
    {
        // sp_res is an error code
        // Don't log, since this function called by logging

        errno = tmp_errno;
        return(nullptr);
    }

    PS_STRLCPY(bname, fname_buff.data(), PST_MAXPATHLEN);
    PS_STRLCAT(bname, ext_buff.data(), PST_MAXPATHLEN);

    errno = tmp_errno;
    return(bname);
}


/* ------------------------------------------------------------------------- */

#else

/* ------------------------------------------------------------------------- */
// Linux or BSD

#include <mutex>
#include <libgen.h> // for basename
#include <string.h> // strlen
#include <stdlib.h> // malloc

/* ------------------------------------------------------------------------- */

#define PS_BASENAME_R ps_basename_r
static std::mutex ps_basename_r_mutex;
extern "C" char * ps_basename_r(const char * path, char * bname)
{
    if (!bname)
        return(nullptr);

    bname[0] = 0;

    std::lock_guard<std::mutex> l_guard(ps_basename_r_mutex);

    char * path_copy =
        reinterpret_cast<char *>(malloc((path ? strlen(path) : 0) + 6));
    strcpy(path_copy, path); // since basename may change path contents

    char * bname_res = basename(path_copy);

    if (bname_res)
       strcpy(&(bname[0]), bname_res);

    free(path_copy);
    return(bname);
}


/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS... else...

/* ------------------------------------------------------------------------- */

#endif // of ifndef __APPLE__
