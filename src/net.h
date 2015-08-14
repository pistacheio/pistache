/* net.h
   Mathieu Stefani, 12 August 2015
   
   Network utility classes
*/

#pragma once
#include <string>
#include <sys/socket.h>

namespace Net {

class Port {
public:
    Port(uint16_t port = 0);

    operator uint16_t() const { return port; }

    bool isReserved() const;
    bool isUsed() const;

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
    Address(Ipv4 ip, Port port);

    Address(const Address& other) = default;
    Address(Address&& other) = default;

    static Address fromUnix(struct sockaddr *addr); 

    std::string host() const;
    Port port() const;

private:
    std::string host_;
    Port port_;
};

} // namespace Net
