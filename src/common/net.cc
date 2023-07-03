/*
 * SPDX-FileCopyrightText: 2015 Mathieu Stefani
 * SPDX-FileCopyrightText: 2023 Andrea Pappacoda
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * net.cc
 * Mathieu Stefani, 12 August 2015
 */

#include <pistache/common.h>
#include <pistache/config.h>
#include <pistache/net.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <cassert>
#include <cstdlib>
#include <cstring>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace Pistache
{
    Port::Port(uint16_t port)
        : port(port)
    { }

    Port::Port(const std::string& data)
    {
        if (data.empty())
            throw std::invalid_argument("Invalid port: empty port");
        char* end     = nullptr;
        long port_num = strtol(data.c_str(), &end, 10);
        if (*end != 0 || port_num < Port::min() || port_num > Port::max())
            throw std::invalid_argument("Invalid port: " + data);
        port = static_cast<uint16_t>(port_num);
    }

    bool Port::isReserved() const { return port < 1024; }

    bool Port::isUsed() const
    {
        throw std::runtime_error("Unimplemented");
        return false;
    }

    std::string Port::toString() const { return std::to_string(port); }

    IP::IP()
    {
        addr_.ss_family = AF_INET6;
    }

    IP::IP(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
    {
        addr_.ss_family      = AF_INET;
        const uint8_t buff[] = { a, b, c, d };
        in_addr_t* in_addr   = &reinterpret_cast<struct sockaddr_in*>(&addr_)->sin_addr.s_addr;

        static_assert(sizeof(buff) == sizeof(*in_addr));
        memcpy(in_addr, buff, sizeof(*in_addr));
    }

    IP::IP(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e, uint16_t f,
           uint16_t g, uint16_t h)
    {
        addr_.ss_family        = AF_INET6;
        const uint16_t buff[8] = { a, b, c, d, e, f, g, h };
        uint16_t remap[8]      = { 0, 0, 0, 0, 0, 0, 0, 0 };
        if (htonl(1) != 1)
        {
            for (int i = 0; i < 8; i++)
            {
                const uint16_t swapped = htons(buff[i]);
                remap[i]               = swapped;
            }
        }
        else
        {
            memcpy(remap, buff, sizeof(remap));
        }
        auto& in6_addr = reinterpret_cast<struct sockaddr_in6*>(&addr_)->sin6_addr.s6_addr;

        static_assert(sizeof(in6_addr) == sizeof(remap));
        memcpy(in6_addr, remap, sizeof(in6_addr));
    }

    IP::IP(const struct sockaddr* addr)
    {
        if (addr->sa_family == AF_INET)
        {
            const struct sockaddr_in* in_addr = reinterpret_cast<const struct sockaddr_in*>(addr);
            struct sockaddr_in* ss_in_addr    = reinterpret_cast<struct sockaddr_in*>(&addr_);

            /* Should this simply be `*ss_in_addr = *in_addr`? */
            ss_in_addr->sin_family      = in_addr->sin_family;
            ss_in_addr->sin_addr.s_addr = in_addr->sin_addr.s_addr;
            ss_in_addr->sin_port        = in_addr->sin_port;
        }
        else if (addr->sa_family == AF_INET6)
        {
            const struct sockaddr_in6* in_addr = reinterpret_cast<const struct sockaddr_in6*>(addr);
            struct sockaddr_in6* ss_in_addr    = reinterpret_cast<struct sockaddr_in6*>(&addr_);

            /* Should this simply be `*ss_in_addr = *in_addr`? */
            ss_in_addr->sin6_family   = in_addr->sin6_family;
            ss_in_addr->sin6_port     = in_addr->sin6_port;
            ss_in_addr->sin6_flowinfo = in_addr->sin6_flowinfo; /* Should be 0 per RFC 3493 */
            memcpy(ss_in_addr->sin6_addr.s6_addr, in_addr->sin6_addr.s6_addr, sizeof(ss_in_addr->sin6_addr.s6_addr));
        }
        else
        {
            throw std::invalid_argument("Invalid socket family");
        }
    }

    IP IP::any() { return IP(0, 0, 0, 0); }

    IP IP::any(bool is_ipv6)
    {
        if (is_ipv6)
        {
            return IP(0, 0, 0, 0, 0, 0, 0, 0);
        }
        else
        {
            return IP(0, 0, 0, 0);
        }
    }

    IP IP::loopback() { return IP(127, 0, 0, 1); }

    IP IP::loopback(bool is_ipv6)
    {
        if (is_ipv6)
        {
            return IP(0, 0, 0, 0, 0, 0, 0, 1);
        }
        else
        {
            return IP(127, 0, 0, 1);
        }
    }

    int IP::getFamily() const { return addr_.ss_family; }

    uint16_t IP::getPort() const
    {
        if (addr_.ss_family == AF_INET)
        {
            return ntohs(reinterpret_cast<const struct sockaddr_in*>(&addr_)->sin_port);
        }
        else if (addr_.ss_family == AF_INET6)
        {
            return ntohs(reinterpret_cast<const struct sockaddr_in6*>(&addr_)->sin6_port);
        }
        else
        {
            unreachable();
        }
    }

    std::string IP::toString() const
    {
        char buff[INET6_ADDRSTRLEN];
        const auto* addr_sa = reinterpret_cast<const struct sockaddr*>(&addr_);
        int err             = getnameinfo(addr_sa, sizeof(addr_), buff, sizeof(buff), NULL, 0, NI_NUMERICHOST);
        if (err) /* [[unlikely]] */
        {
            throw std::runtime_error(gai_strerror(err));
        }
        return std::string(buff);
    }

    void IP::toNetwork(in_addr_t* out) const
    {
        if (addr_.ss_family != AF_INET)
        {
            throw std::invalid_argument("Invalid address family");
        }
        *out = reinterpret_cast<const struct sockaddr_in*>(&addr_)->sin_addr.s_addr;
    }

    void IP::toNetwork(struct in6_addr* out) const
    {
        if (addr_.ss_family != AF_INET)
        {
            throw std::invalid_argument("Invalid address family");
        }
        *out = reinterpret_cast<const struct sockaddr_in6*>(&addr_)->sin6_addr;
    }

    bool IP::supported()
    {
        struct ifaddrs* ifaddr = nullptr;
        struct ifaddrs* ifa    = nullptr;
        int addr_family, n;
        bool supportsIpv6 = false;

        if (getifaddrs(&ifaddr) == -1)
        {
            throw std::runtime_error("Call to getifaddrs() failed");
        }

        for (ifa = ifaddr, n = 0; ifa != nullptr; ifa = ifa->ifa_next, n++)
        {
            if (ifa->ifa_addr == nullptr)
            {
                continue;
            }

            addr_family = ifa->ifa_addr->sa_family;
            if (addr_family == AF_INET6)
            {
                supportsIpv6 = true;
                continue;
            }
        }

        freeifaddrs(ifaddr);
        return supportsIpv6;
    }

    AddressParser::AddressParser(const std::string& data)
    {
        /* If the passed value is a simple IPv6 address as defined by RFC 2373
         * (i.e without port nor '[' and ']'), no custom parsing is required. */
        struct in6_addr tmp;
        if (inet_pton(AF_INET6, data.c_str(), &tmp) == 1)
        {
            char normalized_addr[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &tmp, normalized_addr, sizeof(normalized_addr));
            host_ = normalized_addr;
            family_ = AF_INET6;
            return;
        }

        std::size_t end_pos   = data.find(']');
        std::size_t start_pos = data.find('[');
        if (start_pos != std::string::npos && end_pos != std::string::npos && start_pos < end_pos)
        {
            std::size_t colon_pos = data.find_first_of(':', end_pos);
            if (colon_pos != std::string::npos)
            {
                hasColon_ = true;
            }
            // Strip '[' and ']' in IPv6 addresses, as it is not part of the
            // address itself according to RFC 4291 and RFC 5952, but just a way
            // to represent address + port in an unambiguous way.
            host_   = data.substr(start_pos + 1, end_pos - 1);
            family_ = AF_INET6;
            ++end_pos;
        }
        else
        {
            std::size_t colon_pos = data.find(':');
            if (colon_pos != std::string::npos)
            {
                hasColon_ = true;
            }
            end_pos = colon_pos;
            host_   = data.substr(0, end_pos);
            family_ = AF_INET;
        }

        if (end_pos != std::string::npos && hasColon_)
        {
            port_ = data.substr(end_pos + 1);
            if (port_.empty())
                throw std::invalid_argument("Invalid port");

            // Check if port_ is a valid number
            char* tmp;
            std::strtol(port_.c_str(), &tmp, 10);
            hasNumericPort_ = *tmp == '\0';
        }
    }

    const std::string& AddressParser::rawHost() const { return host_; }

    const std::string& AddressParser::rawPort() const { return port_; }

    bool AddressParser::hasColon() const { return hasColon_; }

    bool AddressParser::hasNumericPort() const { return hasNumericPort_; }

    int AddressParser::family() const { return family_; }

    Address::Address()
        : ip_ {}
        , port_ { 0 }
    { }

    Address::Address(std::string host, Port port)
    {
        std::string addr = std::move(host);
        addr.append(":");
        addr.append(port.toString());
        init(std::move(addr));
    }

    Address::Address(std::string addr) { init(std::move(addr)); }

    Address::Address(const char* addr) { init(std::string(addr)); }

    Address::Address(IP ip, Port port)
        : ip_(ip)
        , port_(port)
    { }

    Address Address::fromUnix(struct sockaddr* addr)
    {
        if ((addr->sa_family == AF_INET) or (addr->sa_family == AF_INET6))
        {
            IP ip     = IP(addr);
            Port port = Port(static_cast<uint16_t>(ip.getPort()));
            assert(addr);
            return Address(ip, port);
        }
        throw Error("Not an IP socket");
    }

    std::string Address::host() const { return ip_.toString(); }

    Port Address::port() const { return port_; }

    int Address::family() const { return ip_.getFamily(); }

    void Address::init(const std::string& addr)
    {
        const AddressParser parser(addr);
        const std::string& host = parser.rawHost();
        const std::string& port = parser.rawPort();

        const bool wildcard = host == "*";

        struct addrinfo hints = {};
        hints.ai_family       = AF_UNSPEC;
        hints.ai_socktype     = SOCK_STREAM;
        hints.ai_protocol     = IPPROTO_TCP;

        if (wildcard)
        {
            hints.ai_flags = AI_PASSIVE;
        }

        // The host is set to nullptr if empty because getaddrinfo() requires
        // it, and also when it is set to "*" because, when combined with the
        // AI_PASSIVE flag, it yields the proper wildcard address. The port, if
        // empty, is set to 80 (http) by default.
        const char* const addrinfo_host = host.empty() || wildcard ? nullptr : host.c_str();
        const char* const addrinfo_port = port.empty() ? "80" : port.c_str();

        AddrInfo addrinfo;
        const int err = addrinfo.invoke(addrinfo_host, addrinfo_port, &hints);
        if (err)
        {
            throw std::invalid_argument(gai_strerror(err));
        }

        const struct addrinfo* result = addrinfo.get_info_ptr();

        ip_   = IP(result->ai_addr);
        port_ = Port(ip_.getPort());

        // Check that the port has not overflowed while calling getaddrinfo()
        if (parser.hasNumericPort() && port_ != std::strtol(addrinfo_port, nullptr, 10))
        {
            throw std::invalid_argument("Invalid numeric port");
        }
    }

    std::ostream& operator<<(std::ostream& os, const Address& address)
    {
        /* As recommended by section 6 of RFC 5952,
         * Notes on Combining IPv6 Addresses with Port Numbers */
        if (address.family() == AF_INET6)
        {
            os << '[';
        }
        os << address.host();
        if (address.family() == AF_INET6)
        {
            os << ']';
        }
        os << ":" << address.port();
        return os;
    }

    Error::Error(const char* message)
        : std::runtime_error(message)
    { }

    Error::Error(std::string message)
        : std::runtime_error(std::move(message))
    { }

    Error Error::system(const char* message)
    {
        const char* err = strerror(errno);

        std::string str(message);
        str += ": ";
        str += err;

        return Error(std::move(str));
    }

} // namespace Pistache
