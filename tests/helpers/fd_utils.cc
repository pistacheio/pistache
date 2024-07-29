/*
 * SPDX-FileCopyrightText: 2023 Mikhail Khachayants
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fd_utils.h"

#if __has_include(<filesystem>)
#include <filesystem>
namespace filesystem = std::filesystem;
#else
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#endif

#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h> // for getpid

// From libproc.h
//   int proc_pidinfo(int pid, int flavor, uint64_t arg, void *buffer,
//                    int buffersize)
//                         __OSX_AVAILABLE_STARTING(__MAC_10_5, __IPHONE_2_0);
//   Parameters:
//     pid - process id
//     flavor - from sys/proc_info.h, PROC_PIDLISTFDS => count fds
//     arg - unused, pass as zero
//     buffer - buff to be filled with proc_fdinfo structs
//     buffersize - size of buffer
//
//   Return: if buffer non-null, number of proc_fdinfo written, -1 on fail

#elif !defined __linux__
#include <unistd.h> // for sysconf

// For getrlimit
#include <sys/resource.h> // Required in FreeBSD+Open+NetBSD
#include <sys/time.h> // recommended in FreeBSD, optional for Open+NetBSD
#include <sys/types.h> // recommended in FreeBSD, optional for Open+NetBSD

#include <string.h> // for memset
#endif

namespace Pistache
{
    std::size_t get_open_fds_count()
    {
#if __APPLE__
        pid_t pid = getpid();

        const int max_fds = 65536;

        for (;;)

        {
            // macOS Sonoma 14.4 March/2024 sizeof(proc_fdinfo) is 8

            char buff[sizeof(proc_fdinfo) * max_fds];

            int buf_used = proc_pidinfo(pid, PROC_PIDLISTFDS, 0,
                                        &(buff[0]), (int)sizeof(buff));
            if (buf_used < 0)
                throw std::runtime_error("proc_pidinfo failed");

#ifdef DEBUG
            if (buf_used % sizeof(proc_fdinfo))
                throw std::runtime_error(
                    "buf_used not a multiple of sizeof(proc_fdinfo)");
#endif

            int num_fds = (buf_used / sizeof(proc_fdinfo));

            if ((num_fds + 1) >= max_fds)
                throw std::runtime_error("num_fds insanely large?");

            return ((std::size_t)num_fds);
        }
#elif defined __linux__
        using filesystem::directory_iterator;
        const filesystem::path fds_dir { "/proc/self/fd" };

        if (!filesystem::exists(fds_dir))
        {
            return directory_iterator::difference_type(0);
        }

        return std::distance(directory_iterator(fds_dir),
                             directory_iterator {});
#else // fallback case, e.g. *BSD
#ifndef OPEN_MAX
#define OPEN_MAX 4096
#endif
        // Be careful with portability here. rl.rlim_cur, of type rlim_t, seems
        // to be an int on FreeBSD, but a wider data type on OpenBSD ("long
        // long" ?). It is signed on FreeBSD and NetBSD, but unsigned on
        // OpenBSD.
        long maxfd;

        maxfd = sysconf(_SC_OPEN_MAX);
        if (maxfd < 0) // or if sysconf not defined at all
        {
            struct rlimit rl;
            memset(&rl, 0, sizeof(rl));
            int getrlimit_res = getrlimit(RLIMIT_NOFILE, &rl);
            if (getrlimit_res == 0)
            {
                maxfd = (long)(rl.rlim_cur < 2 * OPEN_MAX) ? rl.rlim_cur : 2 * OPEN_MAX;
                if (maxfd == 0)
                    maxfd = -1;
            }
        }

        if ((maxfd < 0) || (maxfd > 4 * OPEN_MAX))
            maxfd = OPEN_MAX;

        long j, n = 0;
        for (j = 0; j < maxfd; j++)
        {
            int fd = dup((int)j);
            if (fd < 0)
                continue;
            n++;
            close(fd);
        }

        return (n);
#endif // of ifdef... elif... else...  __APPLE__
    }

} // namespace Pistache
