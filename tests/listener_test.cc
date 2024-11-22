/*
 * SPDX-FileCopyrightText: 2018 Benoit Eudier
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <pistache/winornix.h>
#include PIST_QUOTE(PST_ARPA_INET_HDR)
#include PIST_QUOTE(PST_NETDB_HDR)
#include PIST_QUOTE(PST_NETINET_IN_HDR)


#include <errno.h>
#include <stdlib.h>
#include PIST_QUOTE(PST_SOCKET_HDR)
#include PIST_QUOTE(PIST_SOCKFNS_HDR)

#include <sys/types.h>
#include PIST_QUOTE(PST_MISC_IO_HDR) // unistd.h

#include <array>
#include <sstream>

#include <chrono>
#include <thread> // provides "sleep_for"
using namespace std::chrono_literals;

#include <pistache/ps_strl.h>

#include <pistache/endpoint.h>
#include <pistache/http.h>
#include <pistache/listener.h>

#ifdef _IS_BSD
#include <sys/wait.h> // for wait
#endif

#ifdef _IS_WINDOWS
#include <Windows.h> // for fileapi.h
#include <fileapi.h> // for GetTempPathA
#include <random>
#endif

class SocketWrapper
{

public:
    explicit SocketWrapper(em_socket_t fd)
        : fd_(fd)
    { }
    ~SocketWrapper()
    {
        PS_TIMEDBG_START;
        PST_SOCK_CLOSE(fd_);
    }

    uint16_t port()
    {
        PS_TIMEDBG_START;

        sockaddr_in sin;
        socklen_t len = sizeof(sin);

        uint16_t port = 0;
        if (PST_SOCK_GETSOCKNAME(fd_, reinterpret_cast<struct sockaddr*>(&sin), &len) == -1)
        {
            perror("getsockname");
        }
        else
        {
            port = ntohs(sin.sin_port);
        }
        PS_LOG_DEBUG_ARGS("fd %d, Port %d", fd_, port);

        return port;
    }

private:
    em_socket_t fd_;
};

// Just there for show.
class DummyHandler : public Pistache::Http::Handler
{
public:
    HTTP_PROTOTYPE(DummyHandler)

    void onRequest(const Pistache::Http::Request& /*request*/,
                   Pistache::Http::ResponseWriter response) override
    {
        PS_TIMEDBG_START_THIS;

        response.send(Pistache::Http::Code::Ok, "I am a dummy handler\n");
    }
};

/*
 * Will try to get a free port by binding port 0.
 */
SocketWrapper bind_free_port_helper(int ai_family)
{
    PS_TIMEDBG_START;

    em_socket_t sockfd    = -1; // listen on sock_fd, new connection on new_fd
    struct addrinfo hints = {}, *servinfo, *p;

    PST_SOCK_OPT_VAL_T yes = 1;
    int rv;

    hints.ai_family   = ai_family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE; // use my IP

    PST_SOCK_STARTUP_CHECK;
    if ((rv = getaddrinfo(nullptr, "0", &hints, &servinfo)) != 0)
    {
        if (ai_family == AF_UNSPEC)
        {
            std::cerr << "getaddrinfo: " << gai_strerror(rv) << "\n";
            exit(1);
        }
        throw std::runtime_error("getaddrinfo fail");
    }

    for (p = servinfo; p != nullptr; p = p->ai_next)
    {
        if ((sockfd = PST_SOCK_SOCKET(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            PS_LOG_DEBUG("server: socket");
            if (ai_family == AF_UNSPEC)
                perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
        {
            PS_LOG_DEBUG("setsockopt");
            if (ai_family == AF_UNSPEC)
            {
                perror("setsockopt");
                exit(1);
            }
            throw std::runtime_error("setsockopt fail");
        }

        if (PST_SOCK_BIND(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            PS_LOG_DEBUG_ARGS("server: bind failed, sockfd %d", sockfd);
            PST_SOCK_CLOSE(sockfd);
            if (ai_family == AF_UNSPEC)
                perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (ai_family == AF_UNSPEC)
    {
        if (p == nullptr)
        {
            fprintf(stderr, "server: failed to bind\n");
            exit(1);
        }
        throw std::runtime_error("failed to bind");
    }

    return SocketWrapper(sockfd);
}

/*
 * Will try to get a free port by binding port 0.
 */
SocketWrapper bind_free_port()
{
    // As of July/2024, in Linux and macOS, using AF_UNSPEC leads us to use
    // IPv4 when available. However, in FreeBSD, it causes us to use IPv6 when
    // available. Since Pistache itself defaults to IPv4, we try IPv4 first for
    // bind_free_port_helper, and only try AF_UNSPEC if IPv4 fails.

    try
    {
        return(bind_free_port_helper(AF_INET/*IPv4*/));
    }
    catch(...)
    {
        PS_LOG_DEBUG("bind_free_port_helper failed for IPv4");
    }

    return(bind_free_port_helper(AF_UNSPEC/*any*/));
}


// This is just done to get the value of a free port. The socket will be
// closed after the closing curly bracket and the port will be free again
// (SO_REUSEADDR option). In theory, it is possible that some application grab
// this port before we bind it again...
uint16_t get_free_port() { return bind_free_port().port(); }

TEST(listener_test, listener_bind_port_free)
{
    PS_TIMEDBG_START;

    uint16_t port_nb = get_free_port();

    if (port_nb == 0)
    {
        PS_LOG_DEBUG("Failed to get port");
        FAIL() << "Could not find a free port. Abort test.\n";
    }

    PS_LOG_DEBUG_ARGS("port_nb %u", static_cast<unsigned int>(port_nb));

    Pistache::Port port(port_nb);
    Pistache::Address address(Pistache::Ipv4::any(), port);

    Pistache::Tcp::Listener listener;
    Pistache::Flags<Pistache::Tcp::Options> options;
    listener.init(1, options);
    listener.setHandler(Pistache::Http::make_handler<DummyHandler>());
    listener.bind(address);
    ASSERT_TRUE(true);
}

// Listener should not crash if an additional member is added to the listener
// class. This test is there to prevent regression for PR 303
TEST(listener_test, listener_uses_default)
{
    PS_TIMEDBG_START;

    uint16_t port_nb = get_free_port();

    if (port_nb == 0)
    {
        PS_LOG_DEBUG("Failed to get port");
        FAIL() << "Could not find a free port. Abort test.\n";
    }
    PS_LOG_DEBUG_ARGS("port_nb %u", static_cast<unsigned int>(port_nb));

    Pistache::Port port(port_nb);
    Pistache::Address address(Pistache::Ipv4::any(), port);

    Pistache::Tcp::Listener listener;
    listener.setHandler(Pistache::Http::make_handler<DummyHandler>());
    listener.bind(address);
    ASSERT_TRUE(true);
}

TEST(listener_test, listener_bind_port_not_free_throw_runtime)
{
    PS_TIMEDBG_START;

    SocketWrapper s  = bind_free_port();
    uint16_t port_nb = s.port();

    if (port_nb == 0)
    {
        PS_LOG_DEBUG("Failed to get a free port");
        FAIL() << "Could not find a free port. Abort test.\n";
    }

    Pistache::Port port(port_nb);
    Pistache::Address address(Pistache::Ipv4::any(), port);

    Pistache::Tcp::Listener listener;
    Pistache::Flags<Pistache::Tcp::Options> options;
    listener.init(1, options);
    listener.setHandler(Pistache::Http::make_handler<DummyHandler>());

    try
    {
        listener.bind(address);
        PS_LOG_DEBUG("No std::runtime_error when expected");

        FAIL() << "Expected std::runtime_error while binding, got nothing";
    }
    catch (std::runtime_error const& err)
    {
        PS_TIMEDBG_START;

        std::cout << err.what() << std::endl;
        int flag = 0;

        PST_DECL_SE_ERR_P_EXTRA;
        char * desired_str = // Try strerror_r in case str affected by locale
            PST_STRERROR_R(EADDRINUSE, &se_err[0], sizeof(se_err)-16);
        if ((desired_str) && (strlen(desired_str)) &&
            (strncmp(err.what(), desired_str, strlen(desired_str)) == 0))
        {
            flag = 1;
        }
        else if (strncmp(err.what(), "Address already in use",
                    sizeof("Address already in use")) == 0)
        { // GNU libc
            flag = 1;
        }
        else if (strncmp(err.what(), "Address in use", sizeof("Address in use")) == 0)
        { // Musl libc
            flag = 1;
        }
        else if (strncmp(err.what(), "address in use", sizeof("address in use")) == 0)
        { // MSVS
            flag = 1;
        }
        ASSERT_EQ(flag, 1);
    }
    catch (...)
    {
        PS_LOG_DEBUG("No std::runtime_error when expected");
        FAIL() << "Expected std::runtime_error";
    }
}

// Listener should be able to bind port 0 directly to get an ephemeral port.
TEST(listener_test, listener_bind_ephemeral_v4_port)
{
    PS_TIMEDBG_START;

    Pistache::Port port(0);
    Pistache::Address address(Pistache::Ipv4::any(), port);

    Pistache::Tcp::Listener listener;
    listener.setHandler(Pistache::Http::make_handler<DummyHandler>());
    listener.bind(address);

    Pistache::Port bound_port = listener.getPort();
    ASSERT_TRUE(bound_port > static_cast<uint16_t>(0));
}

TEST(listener_test, listener_bind_ephemeral_v6_port)
{
    PS_TIMEDBG_START;

    Pistache::Tcp::Listener listener;
    if (Pistache::Ipv6::supported())
    {
        Pistache::Port port(0);
        Pistache::Address address(Pistache::Ipv6::any(), port);

        Pistache::Flags<Pistache::Tcp::Options> options;
        listener.setHandler(Pistache::Http::make_handler<DummyHandler>());
        listener.bind(address);

        Pistache::Port bound_port = listener.getPort();
        ASSERT_TRUE(bound_port > static_cast<uint16_t>(0));
    }
    ASSERT_TRUE(true);
}

TEST(listener_test, listener_bind_unix_domain)
{
    PS_TIMEDBG_START;

    // Avoid name conflict by binding within a fresh temporary directory.
#ifdef _IS_WINDOWS
    // No mkdtemp or equivalent in Windows

    const char * tmpDir = 0;
    std::string td_buf_sstr("C:\\temp\\bind_test_852823");

    { // encapsulate
        TCHAR tmp_path[PST_MAXPATHLEN+16];
        tmp_path[0] = 0;
        DWORD gtp_res = GetTempPathA(PST_MAXPATHLEN, &(tmp_path[0]));
        if ((!gtp_res) || (gtp_res > PST_MAXPATHLEN))
        {
            std::cerr << "No temp path found!" << std::endl;
        }
        else
        {
            std::random_device rd;  // a seed source for the engine
            std::mt19937 gen(rd()); // mersenne_twister_engine
            std::uniform_int_distribution<> distrib(100000, 999999);
            auto rnd_6_digits = distrib(gen);

            td_buf_sstr = std::string(&(tmp_path[0])) + "bind_test_" +
                std::to_string(rnd_6_digits);
        }
    }
    tmpDir = td_buf_sstr.c_str();
#else
#define DIR_TEMPLATE "/tmp/bind_test_XXXXXX"
    auto dirTemplate  = std::array<char, sizeof DIR_TEMPLATE> { DIR_TEMPLATE };
    const auto tmpDir = ::mkdtemp(dirTemplate.data());
#endif
    ASSERT_TRUE(tmpDir != nullptr);
    auto ss = std::stringstream();
    ss << tmpDir << "/unix_socket";
    const auto sockName = ss.str();

    struct sockaddr_un sa = {};
    sa.sun_family         = AF_UNIX;
    PS_STRLCPY(sa.sun_path, sockName.c_str(), sizeof sa.sun_path);

    auto address = Pistache::Address::fromUnix(reinterpret_cast<struct sockaddr*>(&sa));
    auto opts    = Pistache::Http::Endpoint::options().threads(2);

    // The test proper.  The Endpoint constructor creates and binds a
    // listening socket with the unix domain address.  It should do so without
    // throwing an exception.
    auto endpoint = Pistache::Http::Endpoint((address));
    endpoint.init(opts);
    endpoint.shutdown();

    // Clean up.
    std::ignore = PST_UNLINK(sockName.c_str());
    std::ignore = PST_RMDIR(tmpDir);
}

#ifndef _WIN32
// CLOEXEC doesn't exist on Windows, and forking is a lower-level system not
// exposed to the user with documented APIs, so these are not really meaningful
// Windows tests
//
// For more inform on the not-officially-documented Windows forking
// capabilities, see:
//   https://github.com/huntandhackett/process-cloning
//   https://captmeelo.com/redteam/maldev/2022/05/10/ntcreateuserprocess.html

class CloseOnExecTest : public testing::Test
{
public:
    ~CloseOnExecTest() override = default;

    std::unique_ptr<Pistache::Tcp::Listener>
    prepare_listener(const Pistache::Tcp::Options options)
    {
        PS_TIMEDBG_START;

        const Pistache::Address address(Pistache::Ipv4::any(),
                                        Pistache::Port(port));
        auto listener = std::make_unique<Pistache::Tcp::Listener>(address);
        listener->setHandler(Pistache::Http::make_handler<DummyHandler>());
        listener->init(1, Pistache::Flags<Pistache::Tcp::Options>(options));
        return listener;
    }

    bool is_child_process(pid_t id)
    {
        constexpr auto fork_child_pid = 0;
        return id == fork_child_pid;
    }

    /*
     * we need to leak the socket through child process and verify that socket is
     * still bound after child has quit
     */
    void try_to_leak_socket(const Pistache::Tcp::Options options)
    {
        PS_TIMEDBG_START;

        pid_t fork_res = fork();
        if (is_child_process(fork_res))
        {
            PS_TIMEDBG_START;

            auto server = prepare_listener(options);
            server->bind();

            // leak open socket to child of our child process
            // static_cast<void> to ignore return value
            [[maybe_unused]] int sys_res = (std::system("sleep 10 <&- &"));
            // Assign result of std::system to suppress Linux warning:
            //   warning: ignoring return value of ‘int system(const char*)’
            //   declared with attribute ‘warn_unused_result’ [-Wunused-result]

            exit(0);
        }

        int status = 0;
        wait(&status);
        ASSERT_EQ(0, status);

        // wait 100 ms, so socket gets a chance to be closed
        std::this_thread::sleep_for(100ms);
    }

    uint16_t port = get_free_port();
};

TEST_F(CloseOnExecTest, socket_not_leaked)
{
    PS_TIMEDBG_START;

    Pistache::Tcp::Options options = Pistache::Tcp::Options::CloseOnExec | Pistache::Tcp::Options::ReuseAddr;

    try_to_leak_socket(options);

    ASSERT_NO_THROW({
        PS_TIMEDBG_START;
        auto server = prepare_listener(options);
        server->bind();
        server->shutdown();
    });
}

TEST_F(CloseOnExecTest, socket_leaked)
{
    PS_TIMEDBG_START;

    Pistache::Tcp::Options options = Pistache::Tcp::Options::ReuseAddr;

    try_to_leak_socket(options);

    ASSERT_THROW(
        {
            PS_TIMEDBG_START;
            auto server = prepare_listener(options);
            server->bind();
            server->shutdown();
        },
        std::runtime_error);
}

#endif // of ifndef _WIN32
