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
#include <netdb.h>

#ifndef _KERNEL_FASTOPEN
#define _KERNEL_FASTOPEN

/* conditional define for TCP_FASTOPEN */
#ifndef TCP_FASTOPEN
#define TCP_FASTOPEN   23
#endif
#endif

namespace Pistache {

// Wrapper around 'getaddrinfo()' that handles cleanup on destruction.
class AddrInfo {
public:
    // Disable copy and assign.
    AddrInfo(const AddrInfo &) = delete;
    AddrInfo& operator=(const AddrInfo &) = delete;

    // Default construction: do nothing.
    AddrInfo() : addrs(nullptr) {}

    ~AddrInfo() {
        if (addrs) {
            ::freeaddrinfo(addrs);
        }
    }

    // Call "::getaddrinfo()", but stash result locally.  Takes the same args
    // as the first 3 args to "::getaddrinfo()" and returns the same result.
    int invoke(const char *node, const char *service,
               const struct addrinfo *hints) {
        if (addrs) {
            ::freeaddrinfo(addrs);
            addrs = nullptr;
        }

        return ::getaddrinfo(node, service, hints, &addrs);
    }

    const struct addrinfo *get_info_ptr() const {
        return addrs;
    }

private:
    struct addrinfo *addrs;
};

class Port {
public:
    Port(uint16_t port = 0);
    explicit Port(const std::string& data);

    operator uint16_t() const { return port; }

    bool isReserved() const;
    bool isUsed() const;
    std::string toString() const;

    static constexpr uint16_t min() {
        return std::numeric_limits<uint16_t>::min();
    }
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
    static Ipv4 loopback();
    std::string toString() const;
    void toNetwork(in_addr_t*) const;

private:
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
};

class Ipv6 {
public:
    Ipv6(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e, uint16_t f, uint16_t g, uint16_t h);

    static Ipv6 any();
    static Ipv6 loopback();

    std::string toString() const;
    void toNetwork(in6_addr*) const;

    // Returns 'true' if the kernel/libc support IPV6, false if not.
    static bool supported();

private:
    uint16_t a;
    uint16_t b;
    uint16_t c;
    uint16_t d;
    uint16_t e;
    uint16_t f;
    uint16_t g;
    uint16_t h;
};

class AddressParser {
public:
    explicit AddressParser(const std::string& data);
    const std::string& rawHost() const;
    const std::string& rawPort() const;
    bool hasColon() const;
    int family() const;
private:
    std::string host_;
    std::string port_;
    bool hasColon_ = false;
    int family_ = 0;
};

class Address {
public:
    Address();
    Address(std::string host, Port port);
    Address(std::string addr);
    Address(const char* addr);
    Address(Ipv4 ip, Port port);
    Address(Ipv6 ip, Port port);

    Address(const Address& other) = default;
    Address(Address&& other) = default;

    Address &operator=(const Address& other) = default;
    Address &operator=(Address&& other) = default;

    static Address fromUnix(struct sockaddr *addr);

    std::string host() const;
    Port port() const;
    int family() const;

private:
    void init(const std::string& addr);
    std::string host_;
    Port port_;
    int family_ = AF_INET;
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
    constexpr size_t operator()(const char (&)[N]) const {

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
