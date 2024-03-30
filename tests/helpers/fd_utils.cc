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
#else
        using filesystem::directory_iterator;
        const filesystem::path fds_dir { "/proc/self/fd" };

        if (!filesystem::exists(fds_dir))
        {
            return directory_iterator::difference_type(0);
        }

        return std::distance(directory_iterator(fds_dir),
                             directory_iterator {});

#endif // of ifdef... else...  __APPLE__
    }

} // namespace Pistache
