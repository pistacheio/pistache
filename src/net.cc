/* net.cc
   Mathieu Stefani, 12 August 2015
   
*/


#include "net.h"
#include "common.h"
#include <stdexcept>
#include <limits>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

namespace Net {

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
    auto pos = addr.find(':');

    if (pos == std::string::npos) 
        throw std::invalid_argument("Invalid address");

    std::string host = addr.substr(0, pos);
    char *end;

    const std::string portPart = addr.substr(pos + 1);
    long port = strtol(portPart.c_str(), &end, 10);
    if (*end != 0 || port > std::numeric_limits<Port>::max())
        throw std::invalid_argument("Invalid port");

    host_ = std::move(host);
    port_ = port;
}

Address::Address(Ipv4 ip, Port port)
    : host_(ip.toString())
    , port_(port)
{ }

Address
Address::fromUnix(struct sockaddr* addr) {
    struct sockaddr_in *in_addr = reinterpret_cast<struct sockaddr_in *>(addr);
    std::string host = TRY_RET(inet_ntoa(in_addr->sin_addr));

    int port = in_addr->sin_port;

    return Address(std::move(host), port);
}

std::string Address::host() const {
    return host_;
}

Port Address::port() const {
    return port_;
}


} // namespace Net
