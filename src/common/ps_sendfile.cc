/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines a Linux-style ps_sendfile when in an OS that does not provide one
// natively (BSD) or with a different interface (Windows)

#include <pistache/ps_sendfile.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

#include <winsock2.h>
#include <mswsock.h> // for TransmitFile
#include <minwinbase.h> // for OVERLAPPED structure
#include <winbase.h> // for HasOverlappedIoCompleted macro
#include <io.h> // _get_osfhandle, _lseeki64

#include <pistache/pist_syslog.h>
/* ------------------------------------------------------------------------- */

extern "C" PST_SSIZE_T ps_sendfile(em_socket_t out_fd, int in_fd,
                                   off_t *offset, size_t count)
{
    __int64 in_fd_start_pos = -1;
    if (offset)
    {
        in_fd_start_pos = _lseeki64(in_fd, 0, SEEK_CUR);
        if (in_fd_start_pos < 0)
        {
            PS_LOG_INFO("lseek error");
            return(-1);
        }
    }
        
    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(overlapped));
    if (offset)
    {
        overlapped.Offset = (DWORD)((*offset) & 0xFFFFFFFF);
        overlapped.OffsetHigh = (DWORD)((*offset) / 0x100000000ull);
    }

    HANDLE in_fd_handle = (HANDLE) _get_osfhandle(in_fd);
    if (in_fd_handle == INVALID_HANDLE_VALUE)
    {
        PS_LOG_INFO_ARGS("Invalid file descriptor %d", in_fd);
        // _get_osfhandle will already have set errno = EBADF
        return(-1);
    }

    BOOL res = TransmitFile(out_fd, in_fd_handle,
                            (DWORD) count, // 0 => transmit whole file
                            0, // nNumberOfBytesPerSend => use default
                            &overlapped,
                            NULL, // lpTransmitBuffers => no pre/suffix buffs
                            0); // flags

    DWORD num_bytes_transferred = 0;
    int last_err = -1;
    
    if (!res)
    {
        last_err = WSAGetLastError();
        if ((last_err == WSA_IO_PENDING ) || (last_err == ERROR_IO_PENDING))
            HasOverlappedIoCompleted(&overlapped);
    }

    if ((res) ||
        (last_err == WSA_IO_PENDING ) || (last_err == ERROR_IO_PENDING))
    {
        // Do this even if res was TRUE (in which case we know it's already
        // completed) so we can populate num_bytes_transferred
        BOOL res_overlapped_result =
            GetOverlappedResult(in_fd_handle, &overlapped,
                                &num_bytes_transferred,
                                TRUE/* wait for completion */);

        if (res_overlapped_result)
        {
            res = TRUE; // success after IO completed
        }
        else
        {
            DWORD last_overlap_err = GetLastError();
            PS_LOG_INFO_ARGS("TransmitFile GetOverlappedResult Windows "
                             "System Error Code (WinError.h) 0x%x",
                             (unsigned int)last_overlap_err);
        }
    }

    if ((offset) && _lseeki64(in_fd, in_fd_start_pos, SEEK_SET) < 0)
    {
        // If offset is non-null, sendfile is not supposed to affect file
        // position
        PS_LOG_INFO("lseek error");
        return(-1);
    }
    
    if (!res)
    {
        errno = EIO;
        return(-1);
    }

    return(num_bytes_transferred);
}

/* ------------------------------------------------------------------------- */

#elif defined(_IS_BSD)

/* ------------------------------------------------------------------------- */

// This is the sendfile function prototype found in Linux. However, sendfile
// does not exist in OpenBSD, so we make our own.
//
// https://www.man7.org/linux/man-pages/man2/sendfile.2.html
// Copies FROM "in_fd" TO "out_fd" Returns number of bytes written on success,
// -1 with errno set on error
//
// If offset is not NULL, then sendfile() does not modify the file offset of
// in_fd; otherwise the file offset is adjusted to reflect the number of bytes
// read from in_fd.
extern "C"
PST_SSIZE_T ps_sendfile(int out_fd, int in_fd, off_t* offset, size_t count)
{
    char buff[65536 + 16]; // 64KB is an efficient size for read/write
                           // blocks in most storage systems. The 16 bytes
                           // is to reduce risk of buffer overflow in the
                           // events of a bug.

    int read_errors               = 0;
    int write_errors              = 0;
    PST_SSIZE_T bytes_written_res = 0;

    off_t in_fd_start_pos = -1;

    if (offset)
    {
        in_fd_start_pos = lseek(in_fd, 0, SEEK_CUR);
        if (in_fd_start_pos < 0)
        {
            PS_LOG_DEBUG("lseek error");
            return (in_fd_start_pos);
        }

        if (lseek(in_fd, *offset, SEEK_SET) < 0)
        {
            PS_LOG_DEBUG("lseek error");
            return (-1);
        }
    }

    for (;;)
    {
        size_t bytes_to_read = count ? std::min(sizeof(buff) - 16, count) : (sizeof(buff) - 16);

        PST_SSIZE_T bytes_read = read(in_fd, &(buff[0]), bytes_to_read);
        if (bytes_read == 0) // End of file
            break;

        if (bytes_read < 0)
        {
            if ((errno == EINTR) || (errno == EAGAIN))
            {
                PS_LOG_DEBUG("read-interrupted error");

                read_errors++;
                if (read_errors < 256)
                    continue;

                PS_LOG_DEBUG("read-interrupted repeatedly error");
                errno = EIO;
            }

            bytes_written_res = -1;
            break;
        }
        read_errors = 0;

        bool re_adjust_pos = false;

        if ((count) && (bytes_read > ((PST_SSIZE_T)count)))
        {
            bytes_read    = ((PST_SSIZE_T)count);
            re_adjust_pos = true;
        }

        if (offset)
        {
            *offset += bytes_read;
            if (re_adjust_pos)
                lseek(in_fd, *offset, SEEK_SET);
        }

        auto p = &(buff[0]);
        while (bytes_read > 0)
        {
            PST_SSIZE_T bytes_written = write(out_fd, p, bytes_read);
            if (bytes_written <= 0)
            {
                if ((bytes_written == 0) || (errno == EINTR) || (errno == EAGAIN))
                {
                    PS_LOG_DEBUG("write-interrupted error");

                    write_errors++;
                    if (write_errors < 256)
                        continue;

                    PS_LOG_DEBUG("write-interrupted repeatedly error");
                    errno = EIO;
                }

                bytes_written_res = -1;
                break;
            }
            write_errors = 0;

            bytes_read -= bytes_written;
            p += bytes_written;
            bytes_written_res += bytes_written;
        }

        if (count)
            count -= bytes_read;
    }

    // if offset non null, set in_fd file pos to pos from start of this
    // function
    if ((offset) && (bytes_written_res >= 0) && (lseek(in_fd, in_fd_start_pos, SEEK_SET) < 0))
    {
        PS_LOG_DEBUG("lseek error");
        bytes_written_res = -1;
    }

    return (bytes_written_res);
}

/* ------------------------------------------------------------------------- */

#else
// Presumably macOS or Linux here
#include <sys/uio.h>

#ifdef __linux__
#include <sys/sendfile.h>
#endif

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS... elif defined(_IS_BSD)... else...
