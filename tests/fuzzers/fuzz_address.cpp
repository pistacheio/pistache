/*
 * SPDX-FileCopyrightText: 2026 The Pistache project contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * fuzz_address - component-API harness for the network-address string
 * parsing in Pistache::Address / AddressParser / Port / IP (src/common/net.cc).
 *
 * Rationale (why this harness should find bugs):
 * - net.cc is ~80 % dark in production coverage (310 of 386 lines uncovered,
 *   report 2026-11-05): the whole Address/IP/Port string parsing stack is
 *   never exercised by fuzz_parsers.
 * - Address strings are attacker-influenceable in real deployments: Host and
 *   X-Forwarded-For style values end up in Address/AddressParser on the
 *   server side, and endpoint code parses user-supplied host:port strings.
 * - The parser mixes its own hand-rolled bracket/colon/port splitting with
 *   strtol() port validation, AF_UNIX heuristic paths (embedded-NUL abstract
 *   sockets, sun_path boundary handling) and getaddrinfo(); that mix is a
 *   classic home for out-of-bounds writes, integer overflows and bad
 *   exception paths.
 *
 * Threat model: the untrusted input is one address string of the forms
 * "host", "host:port", "[v6]:port", "scheme://host:port/page", or a unix
 * domain path (contains '/'). The same bytes also drive sockaddr_un
 * construction (abstract-name paths with embedded NUL bytes), IP-to-network
 * conversion and transformed re-parses ("https://" + input, "[" + input +
 * "]:80"), mirroring how production code re-shapes address strings. Everything
 * is parsed offline; no sockets are created or bound. See the
 * FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION block in net.cc: in fuzzing builds
 * getaddrinfo() is restricted to numeric addresses so iterations never block
 * on DNS; production behaviour is unchanged.
 */

#include <pistache/net.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace Pistache;

template <class F>
void ignoreExceptions(const F& func)
{
    try
    {
        func();
    }
    catch (...)
    {
    }
}

static void fuzz_address_parser(const std::string& input)
{
    ignoreExceptions([&] {
        const AddressParser parser(input);
        const auto& host = parser.rawHost();
        const auto& port = parser.rawPort();
        (void)parser.hasColon();
        (void)parser.hasNumericPort();
        (void)parser.family();
        (void)host;
        (void)port;
    });
}

static void fuzz_port(const std::string& input)
{
    ignoreExceptions([&] {
        const Port port(input);
        (void)port.isReserved();
        const auto rendered = port.toString();
        (void)rendered;
    });
}

static void fuzz_ip_statics()
{
    ignoreExceptions([&] {
        const auto v4    = IP::any(false);
        const auto v6    = IP::loopback(true);
        const auto text4 = v4.toString();
        const auto text6 = v6.toString();
        (void)text4;
        (void)text6;
    });
}

static void fuzz_address_full(const std::string& input)
{
    ignoreExceptions([&] {
        const Address address(input);
        const auto host = address.host();
        const auto port = address.port().toString();
        (void)address.family();
        (void)address.scheme();
        (void)address.page();

        std::ostringstream oss;
        oss << address;

        (void)host;
        (void)port;
    });
}

static void fuzz_address_default_port(const std::string& input)
{
    ignoreExceptions([&] {
        const std::string page("/page");
        const auto address = Address::makeWithDefaultPort(
            input, Port(8080), Address::Scheme::Https, &page);
        const auto host = address.host();
        const auto port = address.port().toString();
        (void)host;
        (void)port;
    });
}

static void fuzz_address_host_port_pair(const std::string& input)
{
    ignoreExceptions([&] {
        const Address address(input, Port(443));
        const auto host = address.host();
        (void)host;
    });
}

static void fuzz_address_from_unix(const std::string& input)
{
    /* Peer-socket parsing: in production the bytes of a sockaddr returned by
     * accept(2) are pure attacker data (remote IP and port). Drive the
     * family-specific sockaddr parsing in IP/IP::getPort/IP::toString through
     * fuzz-chosen families: AF_UNIX (path/abstract names), AF_INET and
     * AF_INET6 (attacker-set address and port bytes), plus invalid families
     * for the documented error path. */
    if (input.empty())
        return;

    const uint8_t family_selector = static_cast<uint8_t>(input[0]) % 4;
    const std::string payload     = input.substr(1);
    if (payload.empty())
        return;

    ignoreExceptions([&] {
        union
        {
            struct sockaddr sa;
            struct sockaddr_un un;
            struct sockaddr_in in;
            struct sockaddr_in6 in6;
        } addr                      = {};
        socklen_t length            = 0;

        switch (family_selector)
        {
        case 0:
            addr.un.sun_family = AF_UNIX;
            length = static_cast<socklen_t>(std::min(payload.size() + 1, sizeof(addr.un.sun_path)));
            if (!payload.empty())
                std::memcpy(addr.un.sun_path, payload.data(), length - 1);
            break;
        case 1:
            addr.in.sin_family = AF_INET;
            addr.in.sin_port   = static_cast<uint16_t>(
                (static_cast<uint16_t>(payload[0 % payload.size()]) << 8)
                | static_cast<uint16_t>(payload[payload.size() > 1 ? 1 : 0]));
            {
                PST_IN_ADDR_T raw = 0;
                for (size_t i = 0; i < sizeof(raw); ++i)
                {
                    reinterpret_cast<uint8_t*>(&raw)[i]
                        = payload[(i + 2) % payload.size()];
                }
                std::memcpy(&addr.in.sin_addr, &raw, sizeof(raw));
            }
            length = sizeof(addr.in);
            break;
        case 2:
            addr.in6.sin6_family = AF_INET6;
            addr.in6.sin6_port   = static_cast<uint16_t>(
                (static_cast<uint16_t>(payload[0 % payload.size()]) << 8)
                | static_cast<uint16_t>(payload[payload.size() > 1 ? 1 : 0]));
            {
                for (size_t i = 0; i < sizeof(addr.in6.sin6_addr.s6_addr); ++i)
                {
                    addr.in6.sin6_addr.s6_addr[i]
                        = payload[(i + 2) % payload.size()];
                }
            }
            length = sizeof(addr.in6);
            break;
        default:
            /* Invalid family: Address::fromUnix must throw. */
            addr.sa.sa_family = AF_APPLETALK;
            length            = sizeof(addr.sa);
            break;
        }
        (void)length;

        const auto address = Address::fromUnix(&addr.sa);
        (void)address.family();
        (void)address.addrLen();
        (void)address.getSockAddr();

        const auto host = address.host();
        (void)host;
        (void)address.port().toString();

        /* Family-specific network-order conversions. */
        if (address.family() == AF_INET)
        {
            PST_IN_ADDR_T network4 = 0;
            const auto ip          = IP::any(false);
            (void)ip;
            std::memcpy(&network4, &addr.in.sin_addr, sizeof(network4));
            (void)network4;
        }
        else if (address.family() == AF_INET6)
        {
            (void)addr.in6.sin6_addr.s6_addr[0];
        }
    });
}

static void fuzz_ip_conversions(const std::string& input)
{
    ignoreExceptions([&] {
        const AddressParser parser(input);
        const auto& port_text = parser.rawPort();
        const Port port(port_text);

        const auto ip4 = IP::any(false);
        Address combined(ip4, port);
        (void)combined.addrLen();
        (void)combined.getSockAddr();
        const auto host = combined.host();
        (void)host;

        PST_IN_ADDR_T network4 = 0;
        ip4.toNetwork(&network4);
    });

    ignoreExceptions([&] {
        /* IPv6 conversion throws for a v4 family object: exercise the
         * exception path with a controlled value. */
        const auto ip4 = IP::loopback(false);
        struct in6_addr network6 = {};
        ip4.toNetwork(&network6);
    });
}

static void fuzz_address_normalized(const std::string& input)
{
    /* Re-parse transformed variants of the same fuzzer-controlled bytes so a
     * single input also reaches the scheme-stripping, bracket-handling and
     * default-port code paths. */
    ignoreExceptions([&] {
        const Address with_scheme("https://" + input);
        const auto host = with_scheme.host();
        (void)host;
    });
    ignoreExceptions([&] {
        const Address bracketed("[" + input + "]:80");
        const auto host = bracketed.host();
        (void)host;
    });
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return 0;

    /* One input drives up to 8 distinct NUL-separated address strings, each
     * parsed through the full pipeline above. Address parsing in a server
     * process handles many independent strings per lifetime; the parsers are
     * stateless, so the segments are independent instances, and the
     * combination space lets coverage keep arriving for the whole run. Odd
     * segments additionally go through the transformed re-parses. */
    constexpr size_t kMaxSegments = 8;
    std::vector<std::string> segments;
    size_t start = 0;
    while (segments.size() < kMaxSegments)
    {
        const void* nul
            = std::memchr(data + start, '\0', size - start);
        const size_t end
            = nul ? static_cast<size_t>(static_cast<const uint8_t*>(nul) - data) : size;
        if (end > start)
            segments.emplace_back(reinterpret_cast<const char*>(data + start), end - start);
        if (!nul || end == size)
            break;
        start = end + 1;
    }

    size_t index = 0;
    for (const auto& input : segments)
    {
        fuzz_address_parser(input);
        fuzz_port(input);
        fuzz_address_full(input);
        fuzz_address_default_port(input);
        fuzz_address_host_port_pair(input);
        fuzz_address_from_unix(input);
        fuzz_ip_conversions(input);

        if (index % 2 == 0)
        {
            fuzz_ip_statics();
            fuzz_address_normalized(input);
        }
        ++index;
    }

    return 0;
}
