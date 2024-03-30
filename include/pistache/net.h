/*
 * SPDX-FileCopyrightText: 2015 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* net.h
   Mathieu Stefani, 12 August 2015

   Network utility classes
*/

#pragma once

#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#ifndef _KERNEL_FASTOPEN
#define _KERNEL_FASTOPEN

/* conditional define for TCP_FASTOPEN */
#ifndef TCP_FASTOPEN
#define TCP_FASTOPEN 23
#endif
#endif

namespace Pistache
{

    // Wrapper around 'getaddrinfo()' that handles cleanup on destruction.
    class AddrInfo
    {
    public:
        // Disable copy and assign.
        AddrInfo(const AddrInfo&)            = delete;
        AddrInfo& operator=(const AddrInfo&) = delete;

        // Default construction: do nothing.
        AddrInfo() = default;

        ~AddrInfo()
        {
            if (addrs)
            {
                ::freeaddrinfo(addrs);
            }
        }

        // Call "::getaddrinfo()", but stash result locally.  Takes the same args
        // as the first 3 args to "::getaddrinfo()" and returns the same result.
        int invoke(const char* node, const char* service,
                   const struct addrinfo* hints)
        {
            if (addrs)
            {
                ::freeaddrinfo(addrs);
                addrs = nullptr;
            }

            return ::getaddrinfo(node, service, hints, &addrs);
        }

        const struct addrinfo* get_info_ptr() const { return addrs; }

    private:
        struct addrinfo* addrs = nullptr;
    };

    class Port
    {
    public:
        Port(uint16_t port = 0);
        explicit Port(const std::string& data);

        operator uint16_t() const { return port; }

        bool isReserved() const;
        bool isUsed() const;
        std::string toString() const;

        static constexpr uint16_t min()
        {
            return std::numeric_limits<uint16_t>::min();
        }
        static constexpr uint16_t max()
        {
            return std::numeric_limits<uint16_t>::max();
        }

    private:
        uint16_t port;
    };

    class IP
    {
    private:
        struct sockaddr_storage addr_ = {};

    public:
        IP();
        IP(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
        IP(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e, uint16_t f,
           uint16_t g, uint16_t h);
        explicit IP(const struct sockaddr*);
        static IP any();
        static IP loopback();
        static IP any(bool ipv6);
        static IP loopback(bool ipv6);
        int getFamily() const;
        uint16_t getPort() const;
        std::string toString() const;
        void toNetwork(in_addr_t*) const;
        void toNetwork(struct in6_addr*) const;
        // Returns 'true' if the system has IPV6 support, false if not.
        static bool supported();
        // Exposes the underlying socket address as a constant struct sockaddr
        // reference.
        const struct sockaddr& getSockAddr() const
        {
            return reinterpret_cast<const struct sockaddr&>(addr_);
        }
    };
    using Ipv4 = IP;
    using Ipv6 = IP;

    class AddressParser
    {
    public:
        explicit AddressParser(const std::string& data);
        const std::string& rawHost() const;
        const std::string& rawPort() const;
        bool hasColon() const;
        bool hasNumericPort() const;
        int family() const;

    private:
        std::string host_;
        std::string port_;
        bool hasColon_       = false;
        bool hasNumericPort_ = false;
        int family_          = 0;
    };

    class Address
    {
    public:
        Address();
        Address(std::string host, Port port);
        Address(std::string host); // retained for backwards compatibility
        
        /*
         * Constructors for creating addresses from strings.  They're
         * typically used to create IP-based addresses, but can also be used
         * to create unix domain socket addresses.  By default the created
         * address will be IP-based.  However, if the addr argument meets one
         * of the criteria below, a unix domain address will result.  Note
         * that matching such a criterion implies that addr would be invalid
         * as an IP-based address.
         *
         * The criteria are:
         *  - addr is empty
         *  - addr[0] == '\0'
         *  - addr contains a '/' character
         */

        explicit Address(const char* addr);

        static Address makeWithDefaultPort(std::string addr,
                                           Port default_port = 0);

        Address(IP ip, Port port);

        Address(const Address& other) = default;
        Address(Address&& other)      = default;

        Address& operator=(const Address& other) = default;
        Address& operator=(Address&& other)      = default;

        /*
         * Supports the AF_INET, AF_INET6, and AF_UNIX address families.
         */
        static Address fromUnix(struct sockaddr* addr);

        std::string host() const;
        Port port() const;
        int family() const;

        /*
         * Returns the address length to be used in calls to bind(2).
         */
        socklen_t addrLen() const
        {
            return addrLen_;
        }

        /*
         * Exposes the underlying socket address as a constant struct sockaddr
         * reference.
         */
        const struct sockaddr& getSockAddr() const
        {
            return ip_.getSockAddr();
        }

        friend std::ostream& operator<<(std::ostream& os, const Address& address);

    private:
        // For init, default_port of zero makes the default port 80, though the
        // default can be overridden by addr
        void init(const std::string& addr, Port default_port);
        void init(const std::string& addr);

        static bool isUnixDomain(const std::string& addr);
        IP ip_;
        Port port_;
        socklen_t addrLen_;
    };

    std::ostream& operator<<(std::ostream& os, const Address& address);

    namespace helpers
    {
        inline Address httpAddr(const std::string_view& view,
                                Port default_port)
        {
            return Address::makeWithDefaultPort(std::string(view),
                                                default_port);
        }

        Address httpAddr(const std::string_view& view);
    } // namespace helpers

    class Error : public std::runtime_error
    {
    public:
        explicit Error(const char* message);
        explicit Error(std::string message);
        static Error system(const char* message);
    };

    template <typename T>
    struct Size
    { };

    template <typename T>
    size_t digitsCount(T val)
    {
        size_t digits = 0;
        while (val % 10)
        {
            ++digits;

            val /= 10;
        }

        return digits;
    }

    template <>
    struct Size<const char*>
    {
        size_t operator()(const char* s) const { return std::strlen(s); }
    };

    template <size_t N>
    struct Size<char[N]>
    {
        constexpr size_t operator()(const char (&)[N]) const
        {

            // We omit the \0
            return N - 1;
        }
    };

#define DEFINE_INTEGRAL_SIZE(Int)        \
    template <>                          \
    struct Size<Int>                     \
    {                                    \
        size_t operator()(Int val) const \
        {                                \
            return digitsCount(val);     \
        }                                \
    }

    DEFINE_INTEGRAL_SIZE(uint8_t);
    DEFINE_INTEGRAL_SIZE(int8_t);
    DEFINE_INTEGRAL_SIZE(uint16_t);
    DEFINE_INTEGRAL_SIZE(int16_t);
    DEFINE_INTEGRAL_SIZE(uint32_t);
    DEFINE_INTEGRAL_SIZE(int32_t);
    DEFINE_INTEGRAL_SIZE(uint64_t);
    DEFINE_INTEGRAL_SIZE(int64_t);

    template <>
    struct Size<bool>
    {
        constexpr size_t operator()(bool) const { return 1; }
    };

    template <>
    struct Size<char>
    {
        constexpr size_t operator()(char) const { return 1; }
    };

} // namespace Pistache
