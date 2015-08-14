/* common.h
   Mathieu Stefani, 12 August 2015
   
   A collection of macro / utilities / constants
*/

#pragma once

#include <sstream>
#include <cstdio>
#include <cassert>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define TRY(...) \
    do { \
        auto ret = __VA_ARGS__; \
        if (ret < 0) { \
            perror(#__VA_ARGS__); \
            cerr << gai_strerror(ret) << endl; \
            return false; \
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

namespace Const {

    static constexpr int MaxBacklog = 128;
    static constexpr int MaxEvents = 1024;
    static constexpr int MaxBuffer = 4096;
    static constexpr int ChunkSize = 1024;
}
