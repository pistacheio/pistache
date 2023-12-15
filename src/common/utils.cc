/*
 * SPDX-FileCopyrightText: 2019 Louis Solofrizzo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* utils.cc
   Louis Solofrizzo 2019-10-17

   Utilities for pistache
*/

#include <pistache/peer.h>
#include <unistd.h>

#ifdef PISTACHE_USE_SSL

ssize_t SSL_sendfile(SSL* out, int in, off_t* offset, size_t count)
{
    unsigned char buffer[4096] = { 0 };
    ssize_t ret;
    ssize_t written;
    size_t to_read;

    if (in == -1)
        return -1;

    to_read = sizeof(buffer) > count ? count : sizeof(buffer);

    if (offset != NULL)
        ret = pread(in, buffer, to_read, *offset);
    else
        ret = read(in, buffer, to_read);

    if (ret == -1)
        return -1;

    written = SSL_write(out, buffer, static_cast<int>(ret));
    if (offset != NULL)
        *offset += written;

    return written;
}

#endif /* PISTACHE_USE_SSL */

#ifdef PISTACHE_USE_BSD_SENDFILE
// Custom function to match arguments with Linux's sendfile.
ssize_t xsendfile(int out, int in, off_t offset, off_t off_bytes)
{    
    size_t bytes = off_bytes;
   	if (bytes > SSIZE_MAX) 
	{
		bytes = SSIZE_MAX;
	} 
    off_t nbytes = 0;
    // no need to include <sys/sendfile> as on Linux
    if (-1 == sendfile(in, out, offset, bytes, NULL, &nbytes, 0)) {
      if (errno == EAGAIN && nbytes > 0) 
      {
	   	return nbytes;
	  }
      return -1;
    }
    return nbytes;
}

#endif /* PISTACHE_USE_BSD_SENDFILE */
