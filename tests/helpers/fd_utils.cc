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

namespace Pistache
{

    std::size_t get_open_fds_count()
    {
        using filesystem::directory_iterator;
        const filesystem::path fds_dir { "/proc/self/fd" };

        if (!filesystem::exists(fds_dir))
        {
            return directory_iterator::difference_type(0);
        }

        return std::distance(directory_iterator(fds_dir), directory_iterator {});
    }

} // namespace Pistache
