/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines a Linux-style ps_sendfile when in an OS that does not provide one
// natively (BSD) or with a different interface (Windows)

#include <pistache/pist_syslog.h>
#include <pistache/ps_sendfile.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

#include <winsock2.h>
#include <mswsock.h> // for TransmitFile
#include <minwinbase.h> // for OVERLAPPED structure
#include <winbase.h> // for HasOverlappedIoCompleted macro
#include <io.h> // _get_osfhandle, _lseeki64

#include <algorithm> // for std::min

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

    auto in_fd_end_fl_pos = _lseeki64(in_fd, 0, SEEK_END);
    if (in_fd_end_fl_pos < 0)
    {
        PS_LOG_INFO("lseek error");
        return(-1);
    }

    // Set offset to starting offset specified by caller if any; otherwise,
    // return file offset to what it was on entry to this function
    off_t offs_to_start =
        offset ? (*offset) : static_cast<off_t>(in_fd_end_fl_pos);
    if (offs_to_start > in_fd_end_fl_pos)
        offs_to_start = static_cast<off_t>(in_fd_end_fl_pos);
    auto set_start_offs_res = _lseeki64(in_fd, offs_to_start, SEEK_SET);
    if (set_start_offs_res < 0)
    {
        PS_LOG_INFO("lseek error");
        return(-1);
    }

    // Note: Per Windows documentation, in TransmitFile (used below) "the
    // transmission of data starts at the current offset in the file" (which of
    // course is why we set the file offset immediately above). The Windows
    // documentation is silent on whether the file offset is updated by
    // TransmitFile.
    //
    // Note: Per Linux documentation:
    //   1/ If offset ptr is NULL, then data will be read from in_fd starting
    //   at the file offset, and the file offset will be updated by the call.
    //   2/ If offset ptr is NOT NULL, then sendfile() will start reading data
    //   from *offset in in_fd.  When sendfile() returns, offset will be set
    //   to the offset of the byte following the last byte that was read, and
    //   sendfile() does NOT modify the file offset of in_fd.

    HANDLE in_fd_handle = reinterpret_cast<HANDLE>(_get_osfhandle(in_fd));
    if (in_fd_handle == INVALID_HANDLE_VALUE)
    {
        PS_LOG_INFO_ARGS("Invalid file descriptor %d", in_fd);
        // _get_osfhandle will already have set errno = EBADF
        return(-1);
    }

    BOOL res = TransmitFile(out_fd, in_fd_handle,
                            static_cast<DWORD>(count), // 0 => whole file
                            0, // nNumberOfBytesPerSend => use default
                            nullptr, // no "overlapped"
                            nullptr, // lpTransmitBuffers => no pre/suffix buffs
                            0); // flags

    DWORD num_bytes_transferred = 0;
    int last_err = -1;

    if (res)
    {
        __int64 final_file_pos = in_fd_end_fl_pos;
        if (count)
            final_file_pos = std::min(offs_to_start+static_cast<off_t>(count),
                                      static_cast<off_t>(in_fd_end_fl_pos));

        num_bytes_transferred =
            static_cast<DWORD>(final_file_pos - offs_to_start);
    }
    else
    {
        last_err = WSAGetLastError();
        PS_LOG_INFO_ARGS("TransmitFile failed, WSAGetLastError %d",
                          last_err);
        errno = EIO;
        return(-1);
    }

    if (offset)
    {
        if (_lseeki64(in_fd, in_fd_start_pos, SEEK_SET) < 0)
        {
            // If offset is non-null, sendfile is not supposed to affect file
            // position
            PS_LOG_INFO("lseek error");
            errno = EIO;
            return(-1);
        }
        *offset = (offs_to_start + num_bytes_transferred);
    }
    else
    {
        if (_lseeki64(in_fd, offs_to_start+num_bytes_transferred, SEEK_SET)< 0)
        {
            // If offset ptr is null, sendfile should make the file offset be
            // immediately after the data that was read from the file
            PS_LOG_INFO("lseek error");
            errno = EIO;
            return(-1);
        }
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

                ++read_errors;
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

                    ++write_errors;
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
