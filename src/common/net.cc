/* net.cc
   Mathieu Stefani, 12 August 2015
   
*/

#include <stdexcept>
#include <limits>
#include <cstring>

#include <netinet/in.h>
#include <arpa/inet.h>
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

std::string
Ipv4::toString() const {
    static constexpr size_t MaxSize = sizeof("255") * 4 + 3 + 1; /* 4 * 255 + 3 * dot + \0 */

    char buff[MaxSize];
    snprintf(buff, MaxSize, "%d.%d.%d.%d", a, b, c, d);

    return std::string(buff);
}

Address::Address()
    : host_("")
    , port_(0)
{ }

Address::Address(std::string host, Port port)
    : host_(std::move(host))
    , port_(port)
{ }


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
{ }

Address
Address::fromUnix(struct sockaddr* addr) {
    struct sockaddr_in *in_addr = reinterpret_cast<struct sockaddr_in *>(addr);

    char *host = inet_ntoa(in_addr->sin_addr);
    assert(addr);

    int port = ntohs(in_addr->sin_port);

    return Address(host, port);
}

std::string
Address::host() const {
    return host_;
}

Port
Address::port() const {
    return port_;
}

void
Address::init(std::string addr) {
    auto pos = addr.find(':');

    if (pos == std::string::npos)
        throw std::invalid_argument("Invalid address");

    std::string host = addr.substr(0, pos);
    char *end;

    const std::string portPart = addr.substr(pos + 1);
    long port = strtol(portPart.c_str(), &end, 10);
    if (*end != 0 || port > Port::max())
        throw std::invalid_argument("Invalid port");

    host_ = std::move(host);
    port_ = port;
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
