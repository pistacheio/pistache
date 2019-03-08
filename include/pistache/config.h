#pragma once

#include <cstddef>

// Allow compile-time overload
namespace Pistache
{
namespace Const
{
    static constexpr size_t MaxBacklog = 128;
    static constexpr size_t MaxEvents  = 1024;
    static constexpr size_t MaxBuffer  = 4096;
    static constexpr size_t DefaultWorkers = 1;

    // Defined from CMakeLists.txt in project root
    static constexpr size_t DefaultMaxPayload = 4096;
    static constexpr size_t ChunkSize  = 1024;
} // namespace Const
} // namespace Pistache