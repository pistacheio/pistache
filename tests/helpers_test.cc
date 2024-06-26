/*
 * SPDX-FileCopyrightText: 2023 Mikhail Khachayants
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "helpers/fd_utils.h"
#include <gtest/gtest.h>

#include <pistache/common.h>
#include <pistache/eventmeth.h>
#include <pistache/os.h>

#include <unistd.h>

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
            int actual_fd = ::socket(AF_INET, SOCK_STREAM, 0);

            if (actual_fd < 0)
                throw std::runtime_error("::socket failed");

            fd_ = Polling::Epoll::em_event_new(actual_fd,
                                               EVM_WRITE | EVM_PERSIST,
                                               FD_CLOEXEC, F_SETFDL_NOTHING);
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

TEST(fd_utils_test, same_result_for_two_calls)
{
    // We do an initial log, since the first time something is logged may cause
    // an additional file descriptor to get allocated (no dount refers to the
    // log file) messing up our counting

    PS_LOG_INFO_ARGS("Initial get_open_fds_count %u", get_open_fds_count());

    const auto count1 = get_open_fds_count();
    const auto count2 = get_open_fds_count();

    ASSERT_EQ(count1, count2);
}

TEST(fd_utils_test, delect_new_descriptor)
{
    const auto count1 = get_open_fds_count();
    const ScopedFd new_fd;
    const auto count2 = get_open_fds_count();

    ASSERT_EQ(count1 + 1, count2);
}

TEST(fd_utils_test, delect_descriptor_close)
{
    ScopedFd fd;
    const auto count1 = get_open_fds_count();
    fd.close();
    const auto count2 = get_open_fds_count();

    ASSERT_EQ(count1, count2 + 1);
}
