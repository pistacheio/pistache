/* net.cc
   Mathieu Stefani, 12 August 2015
   
*/

#include <stdexcept>
#include <limits>
#include <cstring>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <iostream>

#include <pistache/net.h>
#include <pistache/common.h>

using namespace std;

namespace Pistache {

Port::Port(uint16_t port)
    : port(port)
{ }

bool
Port::isReserved() const {
    return port < 1024;
}

bool
Port::isUsed() const {
    throw std::runtime_error("Unimplemented");
    return false;
}

std::string
Port::toString() const {
    return std::to_string(port);
}

Ipv4::Ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
    : a(a)
    , b(b)
    , c(c)
    , d(d)
{ }

Ipv4
Ipv4::any() {
    return Ipv4(0, 0, 0, 0);
}

Ipv4
Ipv4::loopback() {
    return Ipv4(127, 0, 0, 1);
}


std::string
Ipv4::toString() const {
    
    // Use the built-in ipv4 string length from arpa/inet.h
    char buff[INET_ADDRSTRLEN+1];
    
    in_addr_t addr;
    toNetwork(&addr);
    
    // Convert the network format address into display format
    inet_ntop(AF_INET, &addr, buff, INET_ADDRSTRLEN);
    
    return std::string(buff);
}

void Ipv4::toNetwork(in_addr_t *addr) const {
    // Bitshift the bytes into an in_addr_t (a single 32bit unsigned int);
    *addr = htonl( (uint32_t)(a<<24) | (uint32_t)(b<<16) | (uint32_t)(c<<8) | (uint32_t)d );;
}

Ipv6::Ipv6(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e, uint16_t f, uint16_t g, uint16_t h)
    : a(a)
    , b(b)
    , c(c)
    , d(d)
    , e(e)
    , f(f)
    , g(g)
    , h(h)
{ }

Ipv6
Ipv6::any() {
    return Ipv6(0, 0, 0, 0, 0, 0, 0, 0);
}

Ipv6
Ipv6::loopback() {
    return Ipv6(0, 0, 0, 0, 0, 0, 0, 1);
}

std::string
Ipv6::toString() const {
    
    // Use the built-in ipv6 string length from arpa/inet.h
    char buff6[INET6_ADDRSTRLEN+1];
    
    in6_addr addr;
    toNetwork(&addr);
    
    inet_ntop(AF_INET6, &addr, buff6, INET6_ADDRSTRLEN);

    return std::string(buff6);
}

void Ipv6::toNetwork(in6_addr *addr6) const {
    uint16_t temp_ip6[8] = {a, b, c, d, e, f, g, h};
    uint16_t remap_ip6[8] = {0};
    uint16_t x, y;
    
     // If native endianness is little-endian swap the bytes, otherwise just copy them into the new array
    if ( htonl(1) != 1 ) {
        for (uint16_t i = 0; i<8; i++) {
            x = temp_ip6[i];
            y = htons(x);
            remap_ip6[i] = y;
        }
    } else {
        memcpy(remap_ip6, temp_ip6, 16);
    }
    // Copy the bytes into the in6_addr struct
    memcpy(addr6->s6_addr16, remap_ip6, 16);
}

bool Ipv6::supported() {
    struct ifaddrs *ifaddr = nullptr;
    struct ifaddrs *ifa = nullptr;
    int family, n;
    bool supportsIpv6 = false;

    if (getifaddrs(&ifaddr) == -1) {
        throw std::runtime_error("Call to getifaddrs() failed");
    }

    for (ifa = ifaddr, n = 0; ifa != nullptr; ifa = ifa->ifa_next, n++) {
        if (ifa->ifa_addr == nullptr) {
            continue;
        }

        family = ifa->ifa_addr->sa_family;
        if (family == AF_INET6) {
            supportsIpv6 = true;
            continue;
        }
    }

    freeifaddrs(ifaddr);
    return supportsIpv6;
}

Address::Address()
    : host_("")
    , port_(0)
{ }

Address::Address(std::string host, Port port)
{   
    std::string addr = host;
    addr.append(":");
    addr.append(port.toString());
    init(std::move(addr));
}


Address::Address(std::string addr)
{
    init(std::move(addr));
}

Address::Address(const char* addr)
{
    init(std::string(addr));
}

Address::Address(Ipv4 ip, Port port)
    : host_(ip.toString())
    , port_(port)
    , family_(AF_INET)
{ }

Address::Address(Ipv6 ip, Port port)
    : host_(ip.toString())
    , port_(port)
    , family_(AF_INET6)
{ }

Address
Address::fromUnix(struct sockaddr* addr) {
    if (addr->sa_family == AF_INET) { 
        struct sockaddr_in *in_addr = reinterpret_cast<struct sockaddr_in *>(addr);
        char host[INET_ADDRSTRLEN+1];
        inet_ntop(AF_INET, &(in_addr->sin_addr), host, INET_ADDRSTRLEN);
        int port = ntohs(in_addr->sin_port);
        assert(addr);
        return Address(host, port);
    } else if (addr->sa_family == AF_INET6) {
        struct sockaddr_in6 *in_addr = reinterpret_cast<struct sockaddr_in6 *>(addr);
        char host[INET6_ADDRSTRLEN+1];
        inet_ntop(AF_INET6, &(in_addr->sin6_addr), host, INET6_ADDRSTRLEN);
        int port = ntohs(in_addr->sin6_port);
        assert(addr);
        return Address(host, port);
    }
    throw Error("Not an IP socket");    
}

std::string
Address::host() const {
    return host_;
}

Port
Address::port() const {
    return port_;
}

int
Address::family() const {
    return family_;
}

void
Address::init(const std::string& addr) {
    unsigned long pos = addr.find(']');
    unsigned long s_pos = addr.find('[');
    if (pos != std::string::npos && s_pos != std::string::npos) {
        //IPv6 address
        host_ = addr.substr(s_pos+1, pos-1);
        family_ = AF_INET6;
        try {
            in6_addr addr6;
            char buff6[INET6_ADDRSTRLEN+1];
            memcpy(buff6, host_.c_str(), INET6_ADDRSTRLEN);
            inet_pton(AF_INET6, buff6, &(addr6.s6_addr16));
        } catch (std::runtime_error) {
            throw std::invalid_argument("Invalid IPv6 address");
        }
        pos++;
    } else {
        //IPv4 address
        pos = addr.find(':');
        if (pos == std::string::npos)
            throw std::invalid_argument("Invalid address");
        host_ = addr.substr(0, pos);
        family_ = AF_INET;
        if (host_ == "*") {
            host_ = "0.0.0.0";
        }
        try {
            in_addr addr;
            char buff[INET_ADDRSTRLEN+1];
            memcpy(buff, host_.c_str(), INET_ADDRSTRLEN);
            inet_pton(AF_INET, buff, &(addr));
        } catch (std::runtime_error) {
            throw std::invalid_argument("Invalid IPv4 address");
        }
    }
    char *end;
    const std::string portPart = addr.substr(pos + 1);
    if (portPart.empty())
        throw std::invalid_argument("Invalid port");
    long port = strtol(portPart.c_str(), &end, 10);
    if (*end != 0 || port < Port::min() || port > Port::max())
        throw std::invalid_argument("Invalid port");
    port_ = static_cast<uint16_t>(port);
}

Error::Error(const char* message)
    : std::runtime_error(message)
{ }

Error::Error(std::string message)
    : std::runtime_error(std::move(message))
{ }

Error
Error::system(const char* message) {
    const char *err = strerror(errno);

    std::string str(message);
    str += ": ";
    str += err;

    return Error(std::move(str));

}

} // namespace Pistache
