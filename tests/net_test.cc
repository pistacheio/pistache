#include "gtest/gtest.h"

#include <pistache/net.h>

#include <stdexcept>
#include <iostream>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace Pistache;

TEST(net_test, port_creation)
{
    Port port1(3000);
    ASSERT_FALSE(port1.isReserved());
    uint16_t value1 = port1;
    ASSERT_EQ(value1, 3000);
    ASSERT_EQ(port1.toString(), "3000");

    Port port2(80);
    ASSERT_TRUE(port2.isReserved());
    uint16_t value2 = port2;
    ASSERT_EQ(value2, 80);
    ASSERT_EQ(port2.toString(), "80");
}

TEST(net_test, address_creation)
{
    Address address1("127.0.0.1:8080");
    ASSERT_EQ(address1.host(), "127.0.0.1");
    ASSERT_EQ(address1.port(), 8080);

    std::string addr = "127.0.0.1";
    Address address2(addr, Port(8080));
    ASSERT_EQ(address2.host(), "127.0.0.1");
    ASSERT_EQ(address2.port(), 8080);

    Address address3(Ipv4(127, 0, 0, 1), Port(8080));
    ASSERT_EQ(address3.host(), "127.0.0.1");
    ASSERT_EQ(address3.port(), 8080);    

    Address address4(Ipv4::any(), Port(8080));
    ASSERT_EQ(address4.host(), "0.0.0.0");
    ASSERT_EQ(address4.port(), 8080);

    Address address5("*:8080");
    ASSERT_EQ(address4.host(), "0.0.0.0");
    ASSERT_EQ(address4.port(), 8080);
}

TEST(net_test, invalid_address)
{
    ASSERT_THROW(Address("127.0.0.1"), std::invalid_argument);
    ASSERT_THROW(Address("127.0.0.1:9999999"), std::invalid_argument);
    ASSERT_THROW(Address("127.0.0.1:"), std::invalid_argument);
    ASSERT_THROW(Address("127.0.0.1:-10"), std::invalid_argument);
}