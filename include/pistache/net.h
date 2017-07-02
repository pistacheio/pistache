/* net.h
   Mathieu Stefani, 12 August 2015
   
   Network utility classes
*/

#pragma once

#include <string>
#include <cstring>
#include <stdexcept>
#include <limits>

#include <sys/socket.h>

#ifndef _KERNEL_FASTOPEN
#define _KERNEL_FASTOPEN

/* conditional define for TCP_FASTOPEN */
#ifndef TCP_FASTOPEN
#define TCP_FASTOPEN   23
#endif
#endif

namespace Pistache {

class Port {
public:
    Port(uint16_t port = 0);

    operator uint16_t() const { return port; }

    bool isReserved() const;
    bool isUsed() const;

    static constexpr uint16_t max() {
        return std::numeric_limits<uint16_t>::max();
    }

private:
    uint16_t port;
};

class Ipv4 {
public:
    Ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

    static Ipv4 any();
    std::string toString() const;

private:
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
};

class Address {
public:
    Address();
    Address(std::string host, Port port);
    Address(std::string addr);
    Address(const char* addr);
    Address(Ipv4 ip, Port port);

    Address(const Address& other) = default;
    Address(Address&& other) = default;

    Address &operator=(const Address& other) = default;
    Address &operator=(Address&& other) = default;

    static Address fromUnix(struct sockaddr *addr); 

    std::string host() const;
    Port port() const;

private:
    void init(std::string addr);
    std::string host_;
    Port port_;
};

class Error : public std::runtime_error {
public:
    Error(const char* message);
    Error(std::string message);
    static Error system(const char* message);
};

template<typename T>
struct Size { };

template<typename T>
size_t
digitsCount(T val) {
    size_t digits = 0;
    while (val % 10) {
        ++digits;

        val /= 10;
    }

    return digits;
}

template<>
struct Size<const char*> {
    size_t operator()(const char *s) const {
        return std::strlen(s);
    }
};

template<size_t N>
struct Size<char[N]> {
    constexpr size_t operator()(const char (&arr)[N]) const {
        // We omit the \0
        return N - 1;
    }
};

#define DEFINE_INTEGRAL_SIZE(Int) \
    template<> \
    struct Size<Int> { \
        size_t operator()(Int val) const { \
            return digitsCount(val); \
        } \
    }

DEFINE_INTEGRAL_SIZE(uint8_t);
DEFINE_INTEGRAL_SIZE(int8_t);
DEFINE_INTEGRAL_SIZE(uint16_t);
DEFINE_INTEGRAL_SIZE(int16_t);
DEFINE_INTEGRAL_SIZE(uint32_t);
DEFINE_INTEGRAL_SIZE(int32_t);
DEFINE_INTEGRAL_SIZE(uint64_t);
DEFINE_INTEGRAL_SIZE(int64_t);

template<>
struct Size<bool> {
    constexpr size_t operator()(bool) const {
        return 1;
    }
};

template<>
struct Size<char> {
    constexpr size_t operator()(char) const {
        return 1;
    }
};

} // namespace Pistache
