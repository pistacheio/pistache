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
    namespace helpers
    {
        Address httpAddr(const std::string_view& view)
        {
            return(httpAddr(view, 0/*default port*/));
        }
    } // namespace helpers
    
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
        else if (addr->sa_family == AF_UNIX)
        {
            const struct sockaddr_un* un_addr = reinterpret_cast<const struct sockaddr_un*>(addr);
            struct sockaddr_un* ss_un_addr    = reinterpret_cast<struct sockaddr_un*>(&addr_);

            ss_un_addr->sun_family = un_addr->sun_family;
            memcpy(ss_un_addr->sun_path, un_addr->sun_path, sizeof(ss_un_addr->sun_path));
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
        else if (addr_.ss_family == AF_UNIX)
        {
            // Ports are a meaningless concept for unix domain sockets.  Return
            // an arbitrary value.
            return 0;
        }
        else
        {
            unreachable();
        }
    }

    std::string IP::toString() const
    {
        if (addr_.ss_family == AF_UNIX)
        {
            auto& unAddr = reinterpret_cast<const struct sockaddr_un&>(addr_);
            if (unAddr.sun_path[0] == '\0')
            {
                // The socket is abstract (not present in the file system name
                // space).  Its name starts with the byte following the initial
                // NUL.  As the name may contain embedded NUL bytes and its
                // length is not available here, simply note that it's an
                // abstract address.
                return std::string("[Abstract]");
            }
            else
            {
                return std::string(unAddr.sun_path);
            }
        }

        char buff[INET6_ADDRSTRLEN];
        const auto* addr_sa = reinterpret_cast<const struct sockaddr*>(&addr_);
        int err             = getnameinfo(
                        addr_sa, sizeof(addr_), buff, sizeof(buff), NULL, 0, NI_NUMERICHOST);
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
            throw std::invalid_argument("Inapplicable or invalid address family");
        }
        *out = reinterpret_cast<const struct sockaddr_in*>(&addr_)->sin_addr.s_addr;
    }

    void IP::toNetwork(struct in6_addr* out) const
    {
        if (addr_.ss_family != AF_INET)
        {
            throw std::invalid_argument("Inapplicable or invalid address family");
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
            host_   = normalized_addr;
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
            {
                PS_LOG_DEBUG_ARGS("port_ empty, data (addr string) %s, "
                                  "throwing \"Invalid port\"",
                                  data.c_str());

                throw std::invalid_argument("Invalid port");
            }

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
        , addrLen_(sizeof(struct sockaddr_in6))
    { }

    Address::Address(std::string host, Port port)
    {
        std::string addr = std::move(host);
        addr.append(":");
        addr.append(port.toString());
        init(std::move(addr), 0 /* no default port, set explicitly */);
    }

    Address::Address(std::string addr)
    {
        init(std::move(addr), 0 /* default port*/);
    }

    Address::Address(const char* addr)
    {
        init(std::string(addr), 0 /* default port*/);
    }

    Address Address::makeWithDefaultPort(std::string addr,
                                         Port default_port /* defaults to zero*/)
    { // static
        Address res;
        res.init(std::move(addr), default_port);

        return (res);
    }

    Address::Address(IP ip, Port port)
        : ip_(ip)
        , port_(port)
    {
        addrLen_ = ip.getFamily() == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    }

    Address Address::fromUnix(struct sockaddr* addr)
    {
        const auto family = addr->sa_family;
        if (family == AF_INET || family == AF_INET6 || family == AF_UNIX)
        {
            IP ip     = IP(addr);
            Port port = Port(ip.getPort());
            return Address(ip, port);
        }
        throw Error("Not an IP or unix domain socket");
    }

    std::string Address::host() const { return ip_.toString(); }

    Port Address::port() const { return port_; }

    int Address::family() const { return ip_.getFamily(); }

    void Address::init(const std::string& addr)
    {
        init(addr, 0 /*default port*/);
    }
    
    void Address::init(const std::string& addr, Port default_port)
    {
        // Handle unix domain addresses separately.
        if (isUnixDomain(addr))
        {
            struct sockaddr_un unAddr = {};
            unAddr.sun_family         = AF_UNIX;

            // See unix(7) manual page; distinguish among unnamed, abstract,
            // and pathname socket addresses.
            const auto size = std::min(addr.size(), sizeof unAddr.sun_path);
            if (size == 0)
            {
                addrLen_ = sizeof unAddr.sun_family;
            }
            else if (addr[0] == '\0')
            {
                addrLen_ = static_cast<socklen_t>(
                    sizeof unAddr.sun_family + size);
                std::memcpy(unAddr.sun_path, addr.data(), size);
            }
            else
            {
                addrLen_ = static_cast<socklen_t>(
                    offsetof(struct sockaddr_un, sun_path) + size);
                std::strncpy(unAddr.sun_path, addr.c_str(), size);
                if (size == sizeof unAddr.sun_path)
                {
                    unAddr.sun_path[size - 1] = '\0';
                }
            }

            ip_   = IP(reinterpret_cast<struct sockaddr*>(&unAddr));
            port_ = Port(ip_.getPort());
            return;
        }

        if (!default_port)
            default_port = 80;
        std::string default_port_str(std::to_string(default_port));

        addrLen_ = family() == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

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
        const char* const addrinfo_port = port.empty() ? default_port_str.c_str() : port.c_str();

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

    // Applies heuristics to deterimine whether or not addr names a unix
    // domain address.  If it is zero-length, begins with a NUL byte, or
    // contains a '/' character (none of which are possible for legitimate
    // IP-based addresses), it's deemed to be a unix domain address.
    //
    // This heuristic rejects pathname unix domain addresses that contain no
    // '/' characters; such addresses tend not to occur in practice.  See the
    // unix(7) manual page for more infomation.
    bool Address::isUnixDomain(const std::string& addr)
    {
        return addr.size() == 0 || addr[0] == '\0' || addr.find('/') != std::string::npos;
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
