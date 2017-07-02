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
            throw std::runtime_error(oss.str()); \
        } \
        return ret; \
    }(); \
    (void) 0

#define unreachable() __builtin_unreachable()

namespace Pistache {
namespace Const {

    static constexpr int MaxBacklog = 128;
    static constexpr int MaxEvents = 1024;
    static constexpr int MaxBuffer = 4096;
    static constexpr int ChunkSize = 1024;
} // namespace Const
} // namespace Pistache
