/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines certain file functions (operations on an 'int' file descriptor) that
// exist in macOS/Linux/BSD but which need to be defined in Windows
//
#include <pistache/pist_filefns.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

#include <windows.h> // required by fileapi.h
#include <fileapi.h> // ReadFile
#include <minwinbase.h> // for OVERLAPPED structure
#include <winbase.h> // for HasOverlappedIoCompleted macro
#include <io.h> // _get_osfhandle, _lseeki64
#include <share.h> // for _SH_DENYNO used by _sopen_s
#include <fcntl.h> // for _O_CREAT etc.

#include <pistache/pist_syslog.h>

/* ------------------------------------------------------------------------- */

extern "C" PST_SSIZE_T pist_pread(int fd, void *buf,
                                  size_t count, off_t offset)
{
    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(overlapped));

    if (offset)
    {
        overlapped.Offset = (DWORD)(offset & 0xFFFFFFFF);
        overlapped.OffsetHigh = (DWORD)(offset / 0x100000000ull);
    }

    HANDLE fd_handle = (HANDLE) _get_osfhandle(fd);
    if (fd_handle == INVALID_HANDLE_VALUE)
    {
        PS_LOG_INFO_ARGS("Invalid file descriptor %d", fd);
        // _get_osfhandle will already have set errno = EBADF
        return(-1);
    }

    DWORD num_bytes_read = 0;
    BOOL res = ReadFile(fd_handle, buf, (DWORD)count,
                        &num_bytes_read, // but may be wrong if async
                        &overlapped);

    if (!res)
    {
        int last_err = GetLastError();
        if (last_err == ERROR_IO_PENDING)
        {
            HasOverlappedIoCompleted(&overlapped);

            num_bytes_read = 0; // reset it in case ReadFile set it
            BOOL res_overlapped_result =
                GetOverlappedResult(fd_handle, &overlapped,
                                    &num_bytes_read,
                                    TRUE/* wait for completion */);

            if (res_overlapped_result)
            {
                res = TRUE; // success after IO completed
            }
            else
            {
                last_err = GetLastError();
                if (last_err != ERROR_HANDLE_EOF)
                    PS_LOG_INFO_ARGS("ReadFile GetOverlappedResult Windows "
                                     "System Error Code (WinError.h) 0x%x",
                                     (unsigned int) last_err);
            }
        }
        else if (last_err != ERROR_HANDLE_EOF)
        {
            PS_LOG_INFO_ARGS("ReadFile Windows System Error Code "
                             "(WinError.h) 0x%x", (unsigned int) last_err);
        }

        if (last_err == ERROR_HANDLE_EOF)
        {
            PS_LOG_DEBUG("EOF");
            res = TRUE; // successfully reached end of file
            num_bytes_read = 0; // pread rets 0 to indicate EOF
        }
    }

    if (!res)
    {
        PS_LOG_DEBUG("Returning failure");
        errno = EIO;
        return(-1);
    }

    return(num_bytes_read);
}

/* ------------------------------------------------------------------------- */

int pist_open(const char *pathname, int flags)
{
    // Regarding mode, Linux man page states:
    //
    // If neither O_CREAT nor O_TMPFILE is specified in flags, then mode is
    // ignored (and can thus be specified as 0, or simply omitted).  The mode
    // argument must be supplied if O_CREAT or O_TMPFILE is specified in flags

    if (flags & (_O_CREAT | _O_TEMPORARY | _O_SHORT_LIVED))
    {
        PS_LOG_DEBUG("Flags invalid without mode");
        errno = EINVAL;
        return(-1);
    }

    return(pist_open(pathname, flags, 0 /* mode */));
}

int pist_open(const char *pathname, int flags, PST_FILE_MODE_T mode)
{
    int fh = -1;

    errno_t sopen_res = _sopen_s(&fh, pathname, flags, _SH_DENYNO, mode);
    // Re: _SH_DENYNO.
    // The Linux man page for "open" says: "Each open() of a file creates a new
    // open file description; thus, there may be multiple open file
    // descriptions corresponding to a file inode."
    // I think this means that the same process, or multiple processes, can
    // freely open the same file. So we don't block that (aka "we don't lock
    // the file"), which is why we use _SH_DENYNO for the sharing flag.

    if (sopen_res == 0)
        return(fh); // Success

    return(-1); // errno already set by _sopen_s
}


/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS
