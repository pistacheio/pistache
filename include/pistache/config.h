#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

// Allow compile-time overload
namespace Pistache {
namespace Const {
static constexpr size_t MaxBacklog = 128;
static constexpr size_t MaxEvents = 1024;
static constexpr size_t MaxBuffer = 4096;
static constexpr size_t DefaultWorkers = 1;

static constexpr size_t DefaultTimerPoolSize = 128;

// Defined from CMakeLists.txt in project root
static constexpr size_t DefaultMaxRequestSize = 4096;
static constexpr size_t DefaultMaxResponseSize =
    std::numeric_limits<uint32_t>::max();
static constexpr size_t ChunkSize = 1024;

static constexpr uint16_t HTTP_STANDARD_PORT = 80;
} // namespace Const
} // namespace Pistache
