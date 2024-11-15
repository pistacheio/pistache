/*
 * SPDX-FileCopyrightText: 2019 Louis Solofrizzo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* utils.cc
   Louis Solofrizzo 2019-10-17

   Utilities for pistache
*/

#include <pistache/winornix.h>

#include <pistache/peer.h>

#include PIST_QUOTE(PST_MISC_IO_HDR) // unistd.h e.g. close
#include PIST_QUOTE(PIST_FILEFNS_HDR) // for pist_pread

#ifdef PISTACHE_USE_SSL

PST_SSIZE_T SSL_sendfile(SSL* out, int in, off_t* offset, size_t count)
{
    unsigned char buffer[4096] = { 0 };
    PST_SSIZE_T ret;
    PST_SSIZE_T written;
    size_t to_read;

    if (in == -1)
        return -1;

    to_read = sizeof(buffer) > count ? count : sizeof(buffer);

    if (offset != nullptr)
        ret = PST_FILE_PREAD(in, buffer, to_read, *offset);
    else
        ret = PST_FILE_READ(in, buffer, to_read);

    if (ret == -1)
        return -1;

    written = SSL_write(out, buffer, static_cast<int>(ret));
    if (offset != nullptr)
        *offset += (static_cast<off_t>(written));

    return written;
}

#endif /* PISTACHE_USE_SSL */
