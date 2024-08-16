/*
 * SPDX-FileCopyrightText: 2018 knowledge4igor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pistache/async.h>
#include <pistache/client.h>
#include <pistache/common.h>
#include <pistache/endpoint.h>
#include <pistache/http.h>
#include <pistache/peer.h>

#ifdef DEBUG
#include <pistache/eventmeth.h>
#endif

#include <gtest/gtest.h>

#ifdef PISTACHE_USE_CONTENT_ENCODING_BROTLI
#include <brotli/decode.h>
#endif

#ifdef PISTACHE_USE_CONTENT_ENCODING_DEFLATE
#include <zlib.h>
#endif

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <fstream>
#include <future>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>

#include "helpers/fd_utils.h"
#include "tcp_client.h"

using namespace Pistache;
using namespace std::chrono;

namespace
{

    class SimpleLogger
    {
    public:
        static SimpleLogger& instance()
        {
            static SimpleLogger logger;
            return logger;
        }

        void log(const std::string& message)
        {
            std::lock_guard<std::mutex> guard { m_coutLock };
            std::cout << message << std::endl;

            // Save in syslog / os_log as well
            PSLogNoLocFn(LOG_INFO,
                         false, // don't send to stdout - just did that
                         "%s", message.c_str());
        }

    private:
        SimpleLogger() = default;
        std::mutex m_coutLock;
    };

    // from
    // https://stackoverflow.com/questions/9667963/can-i-rewrite-a-logging-macro-with-stream-operators-to-use-a-c-template-functi
    class ScopedLogger
    {
    public:
        ScopedLogger(const std::string& prefix)
        {
            m_stream << "[" << prefix << "] [" << std::hex << std::this_thread::get_id() << "] " << std::dec;
        }

        ~ScopedLogger()
        {
            SimpleLogger::instance().log(m_stream.str());
        }

        std::stringstream& stream()
        {
            return m_stream;
        }

    private:
        std::stringstream m_stream;
    };

#define LOGGER(prefix, message) ScopedLogger(prefix).stream() << message;

}

struct HelloHandlerWithDelay : public Http::Handler
{
    HTTP_PROTOTYPE(HelloHandlerWithDelay)

    explicit HelloHandlerWithDelay(int delay = 0)
        : delay_(delay)
    {
        LOGGER("server", "Init Hello handler with " << delay_ << " seconds delay");
    }

    void onRequest(const Http::Request& /*request*/,
                   Http::ResponseWriter writer) override
    {
        PS_TIMEDBG_START_THIS;

        PS_LOG_DEBUG_ARGS("Sleeping for %ds", delay_);
        std::this_thread::sleep_for(std::chrono::seconds(delay_));

        PS_LOG_DEBUG("Sleep done, calling send");
        writer.send(Http::Code::Ok, "Hello, World!");
    }

    int delay_;
};

constexpr char SLOW_PAGE[] = "/slowpage";

struct HandlerWithSlowPage : public Http::Handler
{
    HTTP_PROTOTYPE(HandlerWithSlowPage)

    explicit HandlerWithSlowPage(int delay = 0)
        : delay_(delay)
    { }

    void onRequest(const Http::Request& request,
                   Http::ResponseWriter writer) override
    {
        PS_TIMEDBG_START_THIS;

        std::string message;
        if (request.resource() == SLOW_PAGE)
        {
            std::this_thread::sleep_for(std::chrono::seconds(delay_));
            message = "[" + std::to_string(counter++) + "] Slow page content!";
        }
        else
        {
            message = "[" + std::to_string(counter++) + "] Hello, World!";
        }

        writer.send(Http::Code::Ok, message);
        LOGGER("server", "Sent: " << message);
    }

    int delay_;
    static std::atomic<size_t> counter;
};

std::atomic<size_t> HandlerWithSlowPage::counter { 0 };

struct FileHandler : public Http::Handler
{
    HTTP_PROTOTYPE(FileHandler)

    explicit FileHandler(const std::string& fileName)
        : fileName_(fileName)
    { }

    void onRequest(const Http::Request& /*request*/,
                   Http::ResponseWriter writer) override
    {
        PS_TIMEDBG_START_THIS;

        Http::serveFile(writer, fileName_)
            .then(
                [this](ssize_t bytes) {
                    LOGGER("server", "Sent " << bytes << " bytes from " << fileName_ << " file");
                },
                Async::IgnoreException);
    }

private:
    std::string fileName_;
};

struct AddressEchoHandler : public Http::Handler
{
    HTTP_PROTOTYPE(AddressEchoHandler)

    AddressEchoHandler() { }

    void onRequest(const Http::Request& request,
                   Http::ResponseWriter writer) override
    {
        PS_TIMEDBG_START_THIS;

        std::string requestAddress = request.address().host();
        writer.send(Http::Code::Ok, requestAddress);
        LOGGER("server", "Sent: " << requestAddress);
    }
};

constexpr const char* ExpectedResponseLine = "HTTP/1.1 408 Request Timeout";

struct PingHandler : public Http::Handler
{
    HTTP_PROTOTYPE(PingHandler)

    PingHandler() = default;

    void onRequest(const Http::Request& request,
                   Http::ResponseWriter writer) override
    {
        PS_TIMEDBG_START_THIS;

        if (request.resource() == "/ping")
        {
            writer.send(Http::Code::Ok, "PONG");
        }
        else
        {
            writer.send(Http::Code::Not_Found);
        }
    }
};

int clientLogicFunc(size_t response_size, const std::string& server_page,
                    int timeout_seconds, int wait_seconds)
{
    PS_TIMEDBG_START;

    Http::Experimental::Client client;
    client.init();

    std::vector<Async::Promise<Http::Response>> responses;
    auto rb = client.get(server_page).timeout(std::chrono::seconds(timeout_seconds));

    // multiple_client_with_requests_to_multithreaded_server could fail
    // intermittently if these counters are not atomic
    int resolver_counter = 0;
    int reject_counter   = 0;
    for (size_t i = 0; i < response_size; ++i)
    {
        PS_TIMEDBG_START;

        auto response = rb.send();
        PS_LOG_DEBUG_ARGS("rb.send() done, i = %u", i);

        response.then(
            [&resolver_counter, pos = i](Http::Response resp) {
                if (resp.code() == Http::Code::Ok)
                {
                    PS_LOG_DEBUG_ARGS("response OK %u", pos);

                    LOGGER("client", "[" << pos << "] Response: " << resp.code() << ", body: `" << resp.body() << "`");
                    ++resolver_counter;
                }
                else
                {
                    PS_LOG_DEBUG_ARGS("response error %u", pos);

                    LOGGER("client", "[" << pos << "] Response: " << resp.code());
                }
            },
            [&reject_counter, pos = i](std::exception_ptr exc) {
                PrintException excPrinter;
                PS_LOG_DEBUG_ARGS("response exception %u", pos);

                LOGGER("client", "[" << pos << "] Reject with reason:");
                excPrinter(exc);
                ++reject_counter;
            });
        responses.push_back(std::move(response));
    }

    {
        PS_TIMEDBG_START;
        auto sync = Async::whenAll(responses.begin(), responses.end());
        Async::Barrier<std::vector<Http::Response>> barrier(sync);
        barrier.wait_for(std::chrono::seconds(wait_seconds));
    }

    client.shutdown();

    LOGGER("client", "resolves: " << resolver_counter << ", rejects: " << reject_counter << ", request timeout: " << timeout_seconds << " seconds" << ", wait: " << wait_seconds << " seconds");

    return resolver_counter;
}

TEST(http_server_test,
     client_disconnection_on_timeout_from_single_threaded_server)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate
        
    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);

    LOGGER("test", "Trying to run server...");
    const int ONE_SECOND_TIMEOUT = 1;
    const int SIX_SECONDS_DELAY  = 6;
    server.setHandler(
        Http::make_handler<HelloHandlerWithDelay>(SIX_SECONDS_DELAY));
    server.serveThreaded();

    const std::string server_address = "localhost:" + server.getPort().toString();
    LOGGER("test", "Server address: " << server_address);

    const int CLIENT_REQUEST_SIZE = 1;
    int counter                   = clientLogicFunc(CLIENT_REQUEST_SIZE, server_address,
                                                    ONE_SECOND_TIMEOUT, SIX_SECONDS_DELAY);

    server.shutdown();

    ASSERT_EQ(counter, 0);

    } // end encapsulate

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}

TEST(
    http_server_test,
    client_multiple_requests_disconnection_on_timeout_from_single_threaded_server)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);

    LOGGER("test", "Trying to run server...");
    const int ONE_SECOND_TIMEOUT = 1;
    const int SIX_SECONDS_DELAY  = 6;
    server.setHandler(
        Http::make_handler<HelloHandlerWithDelay>(SIX_SECONDS_DELAY));
    server.serveThreaded();

    const std::string server_address = "localhost:" + server.getPort().toString();
    LOGGER("test", "Server address: " << server_address);

    const int CLIENT_REQUEST_SIZE = 3;
    int counter                   = clientLogicFunc(CLIENT_REQUEST_SIZE, server_address,
                                                    ONE_SECOND_TIMEOUT, SIX_SECONDS_DELAY);

    server.shutdown();

    ASSERT_EQ(counter, 0);

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}

TEST(http_server_test, multiple_client_with_requests_to_multithreaded_server)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags).threads(3);
    server.init(server_opts);
    LOGGER("test", "Trying to run server...");
    server.setHandler(Http::make_handler<HelloHandlerWithDelay>());
    ASSERT_NO_THROW(server.serveThreaded());

    const std::string server_address = "localhost:" + server.getPort().toString();
    LOGGER("test", "Server address: " << server_address);

    const int NO_TIMEOUT                = 0;
    const int SIX_SECONDS_TIMOUT        = 6;
    const int FIRST_CLIENT_REQUEST_SIZE = 4;
    std::future<int> result1(std::async(clientLogicFunc,
                                        FIRST_CLIENT_REQUEST_SIZE, server_address,
                                        NO_TIMEOUT, SIX_SECONDS_TIMOUT));
    const int SECOND_CLIENT_REQUEST_SIZE = 5;
    std::future<int> result2(
        std::async(clientLogicFunc, SECOND_CLIENT_REQUEST_SIZE, server_address,
                   NO_TIMEOUT, SIX_SECONDS_TIMOUT));

    int res1 = result1.get();
    int res2 = result2.get();

    server.shutdown();

    ASSERT_EQ(res1, FIRST_CLIENT_REQUEST_SIZE);
    ASSERT_EQ(res2, SECOND_CLIENT_REQUEST_SIZE);

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}

TEST(http_server_test, many_client_with_requests_to_multithreaded_server)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags).threads(6);
    server.init(server_opts);
    LOGGER("test", "Trying to run server...");
    server.setHandler(Http::make_handler<HelloHandlerWithDelay>());
    ASSERT_NO_THROW(server.serveThreaded());

    const std::string server_address = "localhost:" + server.getPort().toString();
    LOGGER("test", "Server address: " << server_address);

    const int NO_TIMEOUT                = 0;
    const int SECONDS_TIMOUT            = 20;
    const int FIRST_CLIENT_REQUEST_SIZE = 128;
    std::future<int> result1(std::async(clientLogicFunc,
                                        FIRST_CLIENT_REQUEST_SIZE, server_address,
                                        NO_TIMEOUT, SECONDS_TIMOUT));
    const int SECOND_CLIENT_REQUEST_SIZE = 192;
    std::future<int> result2(
        std::async(clientLogicFunc, SECOND_CLIENT_REQUEST_SIZE, server_address,
                   NO_TIMEOUT, 3 * SECONDS_TIMOUT));

    int res1 = result1.get();
    int res2 = result2.get();

    server.shutdown();

    ASSERT_EQ(res1, FIRST_CLIENT_REQUEST_SIZE);
    ASSERT_EQ(res2, SECOND_CLIENT_REQUEST_SIZE);

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}

TEST(http_server_test,
     multiple_client_with_different_requests_to_multithreaded_server)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags).threads(4);
    server.init(server_opts);
    const int SIX_SECONDS_DELAY = 6;
    server.setHandler(Http::make_handler<HandlerWithSlowPage>(SIX_SECONDS_DELAY));
    server.serveThreaded();

    const std::string server_address = "localhost:" + server.getPort().toString();
    LOGGER("test", "Server address: " << server_address);

    const int FIRST_CLIENT_REQUEST_SIZE = 1;
    const int FIRST_CLIENT_TIMEOUT      = SIX_SECONDS_DELAY / 2;
    std::future<int> result1(std::async(
        clientLogicFunc, FIRST_CLIENT_REQUEST_SIZE, server_address + SLOW_PAGE,
        FIRST_CLIENT_TIMEOUT, SIX_SECONDS_DELAY));
    const int SECOND_CLIENT_REQUEST_SIZE = 2;
    const int SECOND_CLIENT_TIMEOUT      = SIX_SECONDS_DELAY * 2;
    std::future<int> result2(
        std::async(clientLogicFunc, SECOND_CLIENT_REQUEST_SIZE, server_address,
                   SECOND_CLIENT_TIMEOUT, 2 * SIX_SECONDS_DELAY));

    int res1 = result1.get();
    int res2 = result2.get();

    server.shutdown();

    if (hardware_concurrency() > 1)
    {
        ASSERT_EQ(res1, 0);
        ASSERT_EQ(res2, SECOND_CLIENT_REQUEST_SIZE);
    }
    else
    {
        ASSERT_TRUE(true);
    }

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}

TEST(http_server_test, server_with_static_file)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    const std::string data("Hello, World!");
    char fileName[PATH_MAX] = "/tmp/pistacheioXXXXXX";
    if (!mkstemp(fileName))
    {
        std::cerr << "No suitable filename can be generated!" << std::endl;
    }
    LOGGER("test", "Creating temporary file: " << fileName);

    std::ofstream tmpFile;
    tmpFile.open(fileName);
    tmpFile << data;
    tmpFile.close();

    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<FileHandler>(fileName));
    server.serveThreaded();

    const std::string server_address = "localhost:" + server.getPort().toString();
    LOGGER("test", "Server address: " << server_address);

    Http::Experimental::Client client;
    client.init();
    auto rb = client.get(server_address);
    PS_LOG_DEBUG("Calling send");

    auto response = rb.send();
    std::string resultData;
    PS_LOG_DEBUG("About to wait for response");
    response.then(
        [&resultData](Http::Response resp) {
            PS_LOG_DEBUG_ARGS("Http::Response %d", resp.code());

            std::cout << "Response code is " << resp.code() << std::endl;
            if (resp.code() == Http::Code::Ok)
            {
                resultData = resp.body();
            }
        },
        Async::Throw);
    PS_LOG_DEBUG("response.then() returned");

    const int WAIT_TIME = 2;
    Async::Barrier<Http::Response> barrier(response);
    barrier.wait_for(std::chrono::seconds(WAIT_TIME));

    client.shutdown();
    server.shutdown();

    LOGGER("test", "Deleting file " << fileName);
    std::remove(fileName);

    ASSERT_EQ(data, resultData);

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}

TEST(http_server_test, server_request_copies_address)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<AddressEchoHandler>());
    server.serveThreaded();

    const std::string server_address = "localhost:" + server.getPort().toString();
    LOGGER("test", "Server address: " << server_address);

    Http::Experimental::Client client;
    client.init();
    auto rb       = client.get(server_address);
    auto response = rb.send();
    std::string resultData;
    response.then(
        [&resultData](Http::Response resp) {
            LOGGER("client", " Response code is " << resp.code());
            if (resp.code() == Http::Code::Ok)
            {
                resultData = resp.body();
            }
        },
        Async::Throw);

    const int WAIT_TIME = 2;
    Async::Barrier<Http::Response> barrier(response);
    barrier.wait_for(std::chrono::seconds(WAIT_TIME));

    client.shutdown();
    server.shutdown();

    if (address.family() == AF_INET)
    {
        ASSERT_EQ("127.0.0.1", resultData);
    }
    else if (address.family() == AF_INET6)
    {
        ASSERT_EQ("::1", resultData);
    }
    else
    {
        ASSERT_TRUE(false);
    }

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}

struct ResponseSizeHandler : public Http::Handler
{
    HTTP_PROTOTYPE(ResponseSizeHandler)

    explicit ResponseSizeHandler(size_t& rsize, Http::Code& rcode)
        : rsize_(rsize)
        , rcode_(rcode)
    { }

    void onRequest(const Http::Request& request,
                   Http::ResponseWriter writer) override
    {
        PS_TIMEDBG_START_THIS;

        std::string requestAddress = request.address().host();
        writer.send(Http::Code::Ok, requestAddress);
        LOGGER("server", "Sent: " << requestAddress);
        rsize_ = writer.getResponseSize();
        rcode_ = writer.getResponseCode();
    }

    size_t& rsize_;
    Http::Code& rcode_;
};

TEST(http_server_test, response_size_captured)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    const Pistache::Address address("localhost", Pistache::Port(0));

    size_t rsize = 0;
    Http::Code rcode;

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<ResponseSizeHandler>(rsize, rcode));
    server.serveThreaded();

    const std::string server_address = "localhost:" + server.getPort().toString();
    LOGGER("test", "Server address: " << server_address);

    // Use the built-in http client, but this test is interested in testing
    // that the ResponseWriter in the server stashed the correct size and code
    // values.
    Http::Experimental::Client client;
    client.init();
    auto rb       = client.get(server_address);
    auto response = rb.send();
    std::string resultData;
    response.then(
        [&resultData](Http::Response resp) {
            LOGGER("client", "Response code is " << resp.code());
            if (resp.code() == Http::Code::Ok)
            {
                resultData = resp.body();
            }
        },
        Async::Throw);

    const int WAIT_TIME = 2;
    Async::Barrier<Http::Response> barrier(response);
    barrier.wait_for(std::chrono::seconds(WAIT_TIME));

    client.shutdown();
    server.shutdown();

    // Sanity check (stolen from AddressEchoHandler test).
    if (address.family() == AF_INET)
    {
        ASSERT_EQ("127.0.0.1", resultData);
    }
    else if (address.family() == AF_INET6)
    {
        ASSERT_EQ("::1", resultData);
    }
    else
    {
        ASSERT_TRUE(false);
    }

    LOGGER("test", "Response size is " << rsize);
    ASSERT_GT(rsize, 1u);
    ASSERT_LT(rsize, 300u);
    ASSERT_EQ(rcode, Http::Code::Ok);

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}

TEST(http_server_test, client_request_timeout_on_only_connect_raises_http_408)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    Pistache::Address address("localhost", Pistache::Port(0));

    const auto headerTimeout = std::chrono::seconds(2);

    Http::Endpoint server(address);
    auto flags = Tcp::Options::ReuseAddr;
    auto opts  = Http::Endpoint::options()
                    .flags(flags)
                    .headerTimeout(headerTimeout);

    server.init(opts);
    server.setHandler(Http::make_handler<PingHandler>());
    server.serveThreaded();

    auto port = server.getPort();
    auto addr = "localhost:" + port.toString();
    LOGGER("test", "Server address: " << addr)

    TcpClient client;
    EXPECT_TRUE(client.connect(Pistache::Address("localhost", port))) << client.lastError();

    char recvBuf[1024] = {
        0,
    };
    size_t bytes;
    EXPECT_TRUE(client.receive(recvBuf, sizeof(recvBuf), &bytes, std::chrono::seconds(5))) << client.lastError();
    EXPECT_EQ(0, strncmp(recvBuf, ExpectedResponseLine, strlen(ExpectedResponseLine)));

    server.shutdown();

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}

TEST(http_server_test, client_request_timeout_on_delay_in_header_send_raises_http_408)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    Pistache::Address address("localhost", Pistache::Port(0));

    const auto headerTimeout = std::chrono::seconds(1);

    Http::Endpoint server(address);
    auto flags = Tcp::Options::ReuseAddr;
    auto opts  = Http::Endpoint::options()
                    .flags(flags)
                    .headerTimeout(headerTimeout);

    server.init(opts);
    server.setHandler(Http::make_handler<PingHandler>());
    server.serveThreaded();

    auto port = server.getPort();
    auto addr = "localhost:" + port.toString();
    LOGGER("test", "Server address: " << addr);

    const std::string reqStr    = "GET /ping HTTP/1.1\r\n";
    const std::string headerStr = "Host: localhost\r\nUser-Agent: test\r\n";

    TcpClient client;
    EXPECT_TRUE(client.connect(Pistache::Address("localhost", port))) << client.lastError();
    EXPECT_TRUE(client.send(reqStr)) << client.lastError();

    std::this_thread::sleep_for(headerTimeout / 2);
    EXPECT_TRUE(client.send(headerStr)) << client.lastError();

    char recvBuf[1024] = {
        0,
    };
    size_t bytes;
    EXPECT_TRUE(client.receive(recvBuf, sizeof(recvBuf), &bytes, std::chrono::seconds(5))) << client.lastError();
    EXPECT_EQ(0, strncmp(recvBuf, ExpectedResponseLine, strlen(ExpectedResponseLine)));

    server.shutdown();

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}

TEST(http_server_test, client_request_timeout_on_delay_in_request_line_send_raises_http_408)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    Pistache::Address address("localhost", Pistache::Port(0));

    const auto headerTimeout = std::chrono::seconds(2);

    Http::Endpoint server(address);
    auto flags = Tcp::Options::ReuseAddr;
    auto opts  = Http::Endpoint::options()
                    .flags(flags)
                    .headerTimeout(headerTimeout);

    server.init(opts);
    server.setHandler(Http::make_handler<PingHandler>());
    server.serveThreaded();

    auto port = server.getPort();
    auto addr = "localhost:" + port.toString();
    LOGGER("test", "Server address: " << addr);

    const std::string reqStr { "GET /ping HTTP/1.1\r\n" };
    TcpClient client;
    EXPECT_TRUE(client.connect(Pistache::Address("localhost", port))) << client.lastError();
    for (size_t i = 0; i < reqStr.size(); ++i)
    {
        if (!client.send(reqStr.substr(i, 1)))
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    EXPECT_EQ(client.lastErrno(), EPIPE) << "Errno: " << client.lastErrno();

    char recvBuf[1024] = {
        0,
    };
    size_t bytes;
    EXPECT_TRUE(client.receive(recvBuf, sizeof(recvBuf), &bytes, std::chrono::seconds(5))) << client.lastError();
    EXPECT_EQ(0, strncmp(recvBuf, ExpectedResponseLine, strlen(ExpectedResponseLine)));

    server.shutdown();

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}

TEST(http_server_test, client_request_timeout_on_delay_in_body_send_raises_http_408)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    Pistache::Address address("localhost", Pistache::Port(0));

    const auto headerTimeout = std::chrono::seconds(1);
    const auto bodyTimeout   = std::chrono::seconds(2);

    Http::Endpoint server(address);
    auto flags = Tcp::Options::ReuseAddr;
    auto opts  = Http::Endpoint::options()
                    .flags(flags)
                    .headerTimeout(headerTimeout)
                    .bodyTimeout(bodyTimeout);

    server.init(opts);
    server.setHandler(Http::make_handler<PingHandler>());
    server.serveThreaded();

    auto port = server.getPort();
    auto addr = "localhost:" + port.toString();
    LOGGER("test", "Server address: " << addr);

    const std::string reqStr = "POST /ping HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nContent-Length: 32\r\n\r\nabc";

    TcpClient client;
    EXPECT_TRUE(client.connect(Pistache::Address("localhost", port))) << client.lastError();
    EXPECT_TRUE(client.send(reqStr)) << client.lastError();

    char recvBuf[1024] = {
        0,
    };
    size_t bytes;
    EXPECT_TRUE(client.receive(recvBuf, sizeof(recvBuf), &bytes, std::chrono::seconds(5))) << client.lastError();
    EXPECT_EQ(0, strncmp(recvBuf, ExpectedResponseLine, strlen(ExpectedResponseLine)));

    server.shutdown();

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}

TEST(http_server_test, client_request_no_timeout)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    Pistache::Address address("localhost", Pistache::Port(0));

    const auto headerTimeout = std::chrono::seconds(2);
    const auto bodyTimeout   = std::chrono::seconds(4);

    Http::Endpoint server(address);
    auto flags = Tcp::Options::ReuseAddr;
    auto opts  = Http::Endpoint::options()
                    .flags(flags)
                    .headerTimeout(headerTimeout)
                    .bodyTimeout(bodyTimeout);

    server.init(opts);
    server.setHandler(Http::make_handler<PingHandler>());
    server.serveThreaded();

    auto port = server.getPort();
    auto addr = "localhost:" + port.toString();
    LOGGER("test", "Server address: " << addr);

    const std::string headerStr = "POST /ping HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nContent-Length: 8\r\n\r\n";
    const std::string bodyStr   = "abcdefgh\r\n\r\n";

    TcpClient client;
    EXPECT_TRUE(client.connect(Pistache::Address("localhost", port))) << client.lastError();

    std::this_thread::sleep_for(headerTimeout / 2);
    EXPECT_TRUE(client.send(headerStr)) << client.lastError();

    std::this_thread::sleep_for(bodyTimeout / 2);
    EXPECT_TRUE(client.send(bodyStr)) << client.lastError();

    char recvBuf[1024] = {
        0,
    };
    size_t bytes;
    EXPECT_TRUE(client.receive(recvBuf, sizeof(recvBuf), &bytes, std::chrono::seconds(5))) << client.lastError();
    EXPECT_NE(0, strncmp(recvBuf, ExpectedResponseLine, strlen(ExpectedResponseLine)));

    server.shutdown();

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}

namespace
{

    class WaitHelper
    {
    public:
        void increment()
        {
            std::lock_guard<std::mutex> lock(counterLock_);
            ++counter_;
            cv_.notify_one();
        }

        template <typename Duration>
        bool wait(const size_t count, const Duration timeout)
        {
            std::unique_lock<std::mutex> lock(counterLock_);
            return cv_.wait_for(lock, timeout,
                                [this, count]() { return counter_ >= count; });
        }

    private:
        size_t counter_ = 0;
        std::mutex counterLock_;
        std::condition_variable cv_;
    };

    struct ClientCountingHandler : public Http::Handler
    {
        HTTP_PROTOTYPE(ClientCountingHandler)

        explicit ClientCountingHandler(std::shared_ptr<WaitHelper> waitHelper)
            : waitHelper(waitHelper)
        { }

        void onRequest(const Http::Request& request,
                       Http::ResponseWriter writer) override
        {
            PS_TIMEDBG_START_THIS;

            auto peer = writer.getPeer();
            if (peer)
            {
                activeConnections.insert(peer->getID());
            }
            else
            {
                return;
            }
            std::string requestAddress = request.address().host();
            writer.send(Http::Code::Ok, requestAddress);
            LOGGER("server", "Sent `" << requestAddress << "` to " << *peer);
        }

        void onDisconnection(const std::shared_ptr<Tcp::Peer>& peer) override
        {
            LOGGER("server", "Disconnect from " << *peer);
            activeConnections.erase(peer->getID());
            waitHelper->increment();
        }

    private:
        std::unordered_set<size_t> activeConnections;
        std::shared_ptr<WaitHelper> waitHelper;
    };

} // namespace

TEST(http_server_test, client_multiple_requests_disconnects_handled)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);

    std::cout << "Trying to run server...\n";
    auto waitHelper = std::make_shared<WaitHelper>();
    auto handler    = Http::make_handler<ClientCountingHandler>(waitHelper);
    server.setHandler(handler);
    server.serveThreaded();

    const std::string server_address = "localhost:" + server.getPort().toString();
    std::cout << "Server address: " << server_address << "\n";

    const size_t CLIENT_REQUEST_SIZE = 3;
    clientLogicFunc(CLIENT_REQUEST_SIZE, server_address, 1, 6);

    const bool result = waitHelper->wait(CLIENT_REQUEST_SIZE, std::chrono::seconds(2));
    server.shutdown();

    ASSERT_EQ(result, true);

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}

struct ContentEncodingHandler : public Http::Handler
{
    HTTP_PROTOTYPE(ContentEncodingHandler)

    ContentEncodingHandler()
    { }

    // Take whatever the client sent us and send it back compressed...
    void onRequest(const Http::Request& request,
                   Http::ResponseWriter writer) override
    {
        PS_TIMEDBG_START_THIS;

        LOGGER("server", "ContentEncodingHandler::onResponse()");

        // Get the client body...
        const auto client_body = request.body();

        // Compress differently, depending on requested encoding...
        const auto encoding = request.getBestAcceptEncoding();

        // Enable the best compression...
        writer.setCompression(encoding);

        // Set compression level...
        switch (encoding)
        {

#ifdef PISTACHE_USE_CONTENT_ENCODING_BROTLI
        // Set maximum compression if using Brotli
        case Http::Header::Encoding::Br:
            writer.setCompressionBrotliLevel(BROTLI_MAX_QUALITY);
            break;
#endif

#ifdef PISTACHE_USE_CONTENT_ENCODING_DEFLATE
        // Set maximum compression if using deflate/zlib
        case Http::Header::Encoding::Deflate:
            writer.setCompressionDeflateLevel(Z_BEST_COMPRESSION);
            break;
#endif

        default:
            break;
        }

        // Send compressed response of original client body...
        writer.send(Http::Code::Ok, client_body);
    }
};

#ifdef PISTACHE_USE_CONTENT_ENCODING_BROTLI
TEST(http_server_test, server_with_content_encoding_brotli)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    // Data to send to server to expect it to return compressed...

    // Allocate storage...
    std::vector<std::byte> originalUncompressedData(1024);

    // Random bytes engine...
    using random_bytes_engine_type = std::independent_bits_engine<
        std::default_random_engine, CHAR_BIT, unsigned char>;
    random_bytes_engine_type randomEngine;

    // Fill with random bytes...
    std::generate(
        std::begin(originalUncompressedData),
        std::end(originalUncompressedData),
        [&randomEngine]() { return static_cast<std::byte>(randomEngine()); });

    // Bind server to localhost on a random port...
    const Pistache::Address address("localhost", Pistache::Port(0));

    // Initialize server...
    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server_opts.maxRequestSize(1024 * 1024 * 20);
    server_opts.maxResponseSize(1024 * 1024 * 20);
    server.init(server_opts);
    server.setHandler(Http::make_handler<ContentEncodingHandler>());
    server.serveThreaded();

    // Verify server is running...
    ASSERT_TRUE(server.isBound());

    // Log server coordinates...
    const std::string server_address = "localhost:" + server.getPort().toString();
    LOGGER("test", "Server address: " << server_address);

    // Initialize client...

    // Construct and initialize...
    Http::Experimental::Client client;
    client.init();

    // Set server to connect to and get request builder object...
    auto rb = client.get(server_address);

    // Set data to send as body...
    rb.body(
        std::string(
            reinterpret_cast<const char*>(originalUncompressedData.data()),
            originalUncompressedData.size()));

    // Request server send back response Brotli compressed...
    rb.header<Http::Header::AcceptEncoding>(Http::Header::Encoding::Br);

    // Send client request. Note that Transport::asyncSendRequestImpl() is
    //  buggy, or at least with Pistache::Client, when the amount of data being
    //  sent is large. When that happens send() breaks in asyncSendRequestImpl()
    //  receiving an errno=EAGAIN...
    auto response = rb.send();

    // Storage for server response body...
    std::string resultStringData;

    // Verify response code, expected header, and store its body...
    response.then(
        [&resultStringData](Http::Response resp) {
            // Log response code...
            LOGGER("client", "Response code: " << resp.code());

            // Log Content-Encoding header value, if present...
            if (resp.headers().tryGetRaw("Content-Encoding").has_value())
            {
                LOGGER("client", "Content-Encoding: " << resp.headers().tryGetRaw("Content-Encoding").value().value());
            }

            // Preserve body only if response code as expected...
            if (resp.code() == Http::Code::Ok)
                resultStringData = resp.body();

            // Get response headers...
            const auto& headers = resp.headers();

            // Verify Content-Encoding header was present...
            ASSERT_TRUE(headers.has<Http::Header::ContentEncoding>());

            // Verify Content-Encoding was set to Brotli...
            const auto ce = headers.get<Http::Header::ContentEncoding>().get();
            ASSERT_EQ(ce->encoding(), Http::Header::Encoding::Br);
        },
        Async::Throw);

    // Wait for response to complete...
    Async::Barrier<Http::Response> barrier(response);
    barrier.wait();

    // Cleanup client and server...
    client.shutdown();
    server.shutdown();

    // Get server response body in vector...
    std::vector<std::byte> newlyCompressedResponse(resultStringData.size());
    std::transform(
        std::cbegin(resultStringData),
        std::cend(resultStringData),
        std::begin(newlyCompressedResponse),
        [](const char character) { return static_cast<std::byte>(character); });

    // The data the server responded with should be compressed, and therefore
    //  different from the original uncompressed sent during the request...
    ASSERT_NE(originalUncompressedData, newlyCompressedResponse);

    // Decompress response body...

    // Storage for decompressed data...
    std::vector<std::byte> newlyDecompressedData(
        originalUncompressedData.size());

    // Size of destination buffer, but will be updated by uncompress() to
    //  actual size used...
    size_t destinationLength = originalUncompressedData.size();

    // Decompress...
    const auto compressionStatus = ::BrotliDecoderDecompress(
        resultStringData.size(),
        reinterpret_cast<const uint8_t*>(resultStringData.data()),
        &destinationLength,
        reinterpret_cast<uint8_t*>(newlyDecompressedData.data()));

    // Check for failure...
    ASSERT_EQ(compressionStatus, BROTLI_DECODER_RESULT_SUCCESS);

    // The sizes of both the original uncompressed data we sent the server
    //  and the result of decompressing what it sent back should match...
    ASSERT_EQ(originalUncompressedData.size(), destinationLength);

    // Check to ensure the compressed data received back from server after
    //  decompression matches exactly what we originally sent it...
    ASSERT_EQ(originalUncompressedData, newlyDecompressedData);

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}
#endif

#ifdef PISTACHE_USE_CONTENT_ENCODING_DEFLATE
TEST(http_server_test, server_with_content_encoding_deflate)
{
    PS_TIMEDBG_START;

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before = EventMethFns::getEmEventCount();
#endif
#endif

    { // encapsulate

    // Data to send to server to expect it to return compressed...

    // Allocate storage...
    std::vector<std::byte> originalUncompressedData(1024);

    // Random bytes engine...
    using random_bytes_engine_type = std::independent_bits_engine<
        std::default_random_engine, CHAR_BIT, unsigned char>;
    random_bytes_engine_type randomEngine;

    // Fill with random bytes...
    std::generate(
        std::begin(originalUncompressedData),
        std::end(originalUncompressedData),
        [&randomEngine]() { return static_cast<std::byte>(randomEngine()); });

    // Bind server to localhost on a random port...
    const Pistache::Address address("localhost", Pistache::Port(0));

    // Initialize server...
    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server_opts.maxRequestSize(1024 * 1024 * 20);
    server_opts.maxResponseSize(1024 * 1024 * 20);
    server.init(server_opts);
    server.setHandler(Http::make_handler<ContentEncodingHandler>());
    server.serveThreaded();

    // Verify server is running...
    ASSERT_TRUE(server.isBound());

    // Log server coordinates...
    const std::string server_address = "localhost:" + server.getPort().toString();
    LOGGER("test", "Server address: " << server_address);

    // Initialize client...

    // Construct and initialize...
    Http::Experimental::Client client;
    client.init();

    // Set server to connect to and get request builder object...
    auto rb = client.get(server_address);

    // Set data to send as body...
    rb.body(
        std::string(
            reinterpret_cast<const char*>(originalUncompressedData.data()),
            originalUncompressedData.size()));

    // Request server send back response deflate compressed...
    rb.header<Http::Header::AcceptEncoding>(Http::Header::Encoding::Deflate);

    // Send client request. Note that Transport::asyncSendRequestImpl() is
    //  buggy, or at least with Pistache::Client, when the amount of data being
    //  sent is large. When that happens send() breaks in asyncSendRequestImpl()
    //  receiving an errno=EAGAIN...
    auto response = rb.send();

    // Storage for server response body...
    std::string resultStringData;

    // Verify response code, expected header, and store its body...
    response.then(
        [&resultStringData](Http::Response resp) {
            // Log response code...
            LOGGER("client", "Response code: " << resp.code());

            // Log Content-Encoding header value, if present...
            if (resp.headers().tryGetRaw("Content-Encoding").has_value())
            {
                LOGGER("client", "Content-Encoding: " << resp.headers().tryGetRaw("Content-Encoding").value().value());
            }

            // Preserve body only if response code as expected...
            if (resp.code() == Http::Code::Ok)
                resultStringData = resp.body();

            // Get response headers...
            const auto& headers = resp.headers();

            // Verify Content-Encoding header was present...
            ASSERT_TRUE(headers.has<Http::Header::ContentEncoding>());

            // Verify Content-Encoding was set to deflate...
            const auto ce = headers.get<Http::Header::ContentEncoding>().get();
            ASSERT_EQ(ce->encoding(), Http::Header::Encoding::Deflate);
        },
        Async::Throw);

    // Wait for response to complete...
    Async::Barrier<Http::Response> barrier(response);
    barrier.wait();

    // Cleanup client and server...
    client.shutdown();
    server.shutdown();

    // Get server response body in vector...
    std::vector<std::byte> newlyCompressedResponse(resultStringData.size());
    std::transform(
        std::cbegin(resultStringData),
        std::cend(resultStringData),
        std::begin(newlyCompressedResponse),
        [](const char character) { return static_cast<std::byte>(character); });

    // The data the server responded with should be compressed, and therefore
    //  different from the original uncompressed sent during the request...
    ASSERT_NE(originalUncompressedData, newlyCompressedResponse);

    // Decompress response body...

    // Storage for decompressed data...
    std::vector<std::byte> newlyDecompressedData(
        originalUncompressedData.size());

    // Size of destination buffer, but will be updated by uncompress() to
    //  actual size used...
    unsigned long destinationLength = originalUncompressedData.size();

    // Decompress...
    const auto compressionStatus = ::uncompress(
        reinterpret_cast<unsigned char*>(newlyDecompressedData.data()),
        &destinationLength,
        reinterpret_cast<const unsigned char*>(resultStringData.data()),
        resultStringData.size());

    // Check for failure...
    ASSERT_EQ(compressionStatus, Z_OK);

    // The sizes of both the original uncompressed data we sent the server
    //  and the result of decompressing what it sent back should match...
    ASSERT_EQ(originalUncompressedData.size(), destinationLength);

    // Check to ensure the compressed data received back from server after
    //  decompression matches exactly what we originally sent it...
    ASSERT_EQ(originalUncompressedData, newlyDecompressedData);

    } // end encapsulate
    
#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after = EventMethFns::getEmEventCount();

    PS_LOG_DEBUG_ARGS("em_event_count_before %d, em_event_count_after %d",
                      em_event_count_before, em_event_count_after);
    ASSERT_EQ(em_event_count_before, em_event_count_after);
#endif
#endif
}
#endif

TEST(http_server_test, http_server_is_not_leaked)
{
    PS_TIMEDBG_START;

    { // encapsulate

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_before               = EventMethFns::getEmEventCount();
    const int libevent_event_count_before         = EventMethFns::getLibeventEventCount();
    const int event_meth_epoll_equiv_count_before = EventMethFns::getEventMethEpollEquivCount();
    const int event_meth_base_count_before        = EventMethFns::getEventMethBaseCount();
    const int wait_then_get_count_before          = EventMethFns::getWaitThenGetAndEmptyReadyEvsCount();
#endif
#endif

    const auto fds_before = get_open_fds_count();
    const Pistache::Address address("localhost", Pistache::Port(0));

    auto server      = std::make_unique<Http::Endpoint>(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags).threads(4);
    server->init(server_opts);
    server->setHandler(Http::make_handler<PingHandler>());
    server->serveThreaded();

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_during               = EventMethFns::getEmEventCount();
    const int libevent_event_count_during         = EventMethFns::getLibeventEventCount();
    const int event_meth_epoll_equiv_count_during = EventMethFns::getEventMethEpollEquivCount();
    const int event_meth_base_count_during        = EventMethFns::getEventMethBaseCount();
    const int wait_then_get_count_during          = EventMethFns::getWaitThenGetAndEmptyReadyEvsCount();
#endif
#endif

    server->shutdown();
    server.reset();

    const auto fds_after = get_open_fds_count();
    ASSERT_EQ(fds_before, fds_after);

#ifdef _USE_LIBEVENT_LIKE_APPLE
#ifdef DEBUG
    const int em_event_count_after               = EventMethFns::getEmEventCount();
    const int libevent_event_count_after         = EventMethFns::getLibeventEventCount();
    const int event_meth_epoll_equiv_count_after = EventMethFns::getEventMethEpollEquivCount();
    const int event_meth_base_count_after        = EventMethFns::getEventMethBaseCount();
    const int wait_then_get_count_after          = EventMethFns::getWaitThenGetAndEmptyReadyEvsCount();

    PS_LOG_DEBUG_ARGS(
        "em_event_count_before %d, em_event_count_during %d, "
        "em_event_count_after %d; "
        "libevent_event_count_before %d, libevent_event_count_during %d, "
        "libevent_event_count_after %d; "
        "event_meth_epoll_equiv_count_before %d, "
        "event_meth_epoll_equiv_count_during %d, "
        "event_meth_epoll_equiv_count_after %d; "
        "event_meth_base_count_before %d, event_meth_base_count_during %d, "
        "event_meth_base_count_after %d; "
        "wait_then_get_count_before %d, wait_then_get_count_during %d, "
        "wait_then_get_count_after %d; ",
        em_event_count_before, em_event_count_during, em_event_count_after,
        libevent_event_count_before, libevent_event_count_during,
        libevent_event_count_after,
        event_meth_epoll_equiv_count_before,
        event_meth_epoll_equiv_count_during, event_meth_epoll_equiv_count_after,
        event_meth_base_count_before, event_meth_base_count_during,
        event_meth_base_count_after,
        wait_then_get_count_before, wait_then_get_count_during,
        wait_then_get_count_after);

    ASSERT_EQ(em_event_count_before, em_event_count_after);
    ASSERT_EQ(libevent_event_count_before, libevent_event_count_after);
    ASSERT_EQ(event_meth_epoll_equiv_count_before,
              event_meth_epoll_equiv_count_after);
    ASSERT_EQ(event_meth_base_count_before, event_meth_base_count_after);
    ASSERT_EQ(wait_then_get_count_before, wait_then_get_count_after);

#endif
#endif

    } // end encapsulate
}
