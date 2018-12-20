/* common.h
   Mathieu Stefani, 12 August 2015

   A collection of macro / utilities / constants
*/

#pragma once

#include <sstream>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <stdexcept>

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

#define unsafe

#define TRY(...) \
    do { \
        auto ret = __VA_ARGS__; \
        if (ret < 0) { \
            const char* str = #__VA_ARGS__; \
            std::ostringstream oss; \
            oss << str << ": "; \
            if (errno == 0) { \
                oss << gai_strerror(ret); \
            } else { \
                oss << strerror(errno); \
            } \
            oss << " (" << __FILE__ << ":" << __LINE__ << ")"; \
            throw std::runtime_error(oss.str()); \
        } \
    } while (0)

#define TRY_RET(...) \
    [&]() { \
        auto ret = __VA_ARGS__; \
        if (ret < 0) { \
            const char *str = #__VA_ARGS__; \
            std::ostringstream oss; \
            oss << str << ": " << strerror(errno); \
            oss << " (" << __FILE__ << ":" << __LINE__ << ")"; \
            throw std::runtime_error(oss.str()); \
        } \
        return ret; \
    }(); \
    (void) 0

#define unreachable() __builtin_unreachable()

// Until we require C++17 compiler with [[maybe_unused]]
#define UNUSED(x) (void)(x);

// Allow compile-time overload
namespace Pistache {
namespace Const {

    static constexpr size_t MaxBacklog = 128;
    static constexpr size_t MaxEvents  = 1024;
    static constexpr size_t MaxBuffer  = 4096;
    static constexpr size_t DefaultWorkers = 1;

    // Defined from CMakeLists.txt in project root
    static constexpr size_t DefaultMaxPayload = 4096;
    static constexpr size_t ChunkSize  = 1024;
} // namespace Const
} // namespace Pistache
