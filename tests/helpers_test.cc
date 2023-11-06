/*
 * SPDX-FileCopyrightText: 2023 Mikhail Khachayants
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "helpers/fd_utils.h"
#include <gtest/gtest.h>
#include <sys/eventfd.h>
#include <unistd.h>

using namespace Pistache;

namespace
{

    class ScopedFd
    {
    public:
        ScopedFd()
            : fd_(::eventfd(0, EFD_CLOEXEC))
        { }

        void close()
        {
            if (fd_ != -1)
            {
                ::close(fd_);
                fd_ = -1;
            }
        }

        ~ScopedFd()
        {
            close();
        }

    private:
        int fd_;
    };

} // namespace

TEST(fd_utils_test, same_result_for_two_calls)
{
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
