/*
 * SPDX-FileCopyrightText: 2023 Mikhail Khachayants
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "helpers/fd_utils.h"
#include <gtest/gtest.h>

#include <pistache/winornix.h>

#include <pistache/common.h>
#include <pistache/eventmeth.h>
#include <pistache/os.h>

#include PIST_QUOTE(PIST_SOCKFNS_HDR) // e.g. unistd.h

using namespace Pistache;

namespace
{

    class ScopedFd
    {
    public:
        ScopedFd()
            : fd_(PS_FD_EMPTY)
        {
#ifdef _USE_LIBEVENT

            // em_event_new does not allocate an actual fd, so we provide
            // one to achieve the same effect
            em_socket_t actual_fd = PST_SOCK_SOCKET(AF_INET, SOCK_STREAM, 0);

            if (actual_fd < 0)
                throw std::runtime_error("::socket failed");

            fd_ = Polling::Epoll::em_event_new(actual_fd,
                                               EVM_WRITE | EVM_PERSIST,
                                               PST_FD_CLOEXEC,
                                               F_SETFDL_NOTHING);
            if (fd_ == PS_FD_EMPTY)
                throw std::runtime_error("Epoll::em_event_new failed");

#else
            fd_ = ::eventfd(0, EFD_CLOEXEC);
            if (fd_ == PS_FD_EMPTY)
                throw std::runtime_error("::eventfd failed");
#endif
        }

        void close()
        {
            if (fd_ != PS_FD_EMPTY)
            {
                CLOSE_FD(fd_);
                fd_ = PS_FD_EMPTY;
            }
        }

        ~ScopedFd()
        {
            close();
        }

    private:
        Fd fd_;
    };

} // namespace

#if defined(_WIN32) && defined(__MINGW32__) && defined(DEBUG)
    // In this special case, we allow the number of FDs in use to grow by
    // one. This may be related to the use of GetModuleHandleA to load
    // KernelBase.dll. Or not. We have only seen the number of file handles in
    // use grow in DEBUG mode, so it is also possible it's related to Windows
    // logging.
#define ALLOW_OPEN_FDS_TO_GROW_BY_ONE 1
#endif

TEST(fd_utils_test, same_result_for_two_calls)
{
    // We do an initial log, since the first time something is logged may cause
    // an additional file descriptor to get allocated (no dount refers to the
    // log file) messing up our counting

    PS_LOG_INFO_ARGS("Initial get_open_fds_count %u", get_open_fds_count());

    const auto count1 = get_open_fds_count();
    const auto count2 = get_open_fds_count();

#ifdef ALLOW_OPEN_FDS_TO_GROW_BY_ONE
    if ((count1+1) == count2)
        ASSERT_EQ(count1+1, count2);
    else
#endif
        ASSERT_EQ(count1, count2);
}

TEST(fd_utils_test, delect_new_descriptor)
{
    const auto count1 = get_open_fds_count();
    const ScopedFd new_fd;
    const auto count2 = get_open_fds_count();
#ifdef _WIN32
    // Doing a winsock "socket" call to allocate a socket handle actually seems
    // to use up 7 handles in total (Windows 11, Sept/2024)
    ASSERT_GT(count2, count1);
    ASSERT_GT(count1+32, count2);
#else
    ASSERT_EQ(count1 + 1, count2);
#endif
}

TEST(fd_utils_test, delect_descriptor_close)
{
    ScopedFd fd;
    const auto count1 = get_open_fds_count();
    fd.close();
    const auto count2 = get_open_fds_count();

#ifdef ALLOW_OPEN_FDS_TO_GROW_BY_ONE
    if (count1 == count2)
        ASSERT_EQ(count1, count2);
    else
#endif
        ASSERT_EQ(count1, count2 + 1);
}
