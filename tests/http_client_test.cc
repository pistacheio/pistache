#include <pistache/http.h>
#include <pistache/client.h>
#include <pistache/endpoint.h>

#include "gtest/gtest.h"

#include <atomic>
#include <chrono>

using namespace Pistache;

struct HelloHandler : public Http::Handler
{
    HTTP_PROTOTYPE(HelloHandler)

    void onRequest(const Http::Request& /*request*/, Http::ResponseWriter writer) override
    {
        writer.send(Http::Code::Ok, "Hello, World!");
    }
};

struct DelayHandler : public Http::Handler
{
    HTTP_PROTOTYPE(DelayHandler)

    void onRequest(const Http::Request& /*request*/, Http::ResponseWriter writer) override
    {
        std::this_thread::sleep_for(std::chrono::seconds(4));
        writer.send(Http::Code::Ok, "Hello, World!");
    }
};

TEST(http_client_test, one_client_with_one_request)
{
    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.serveThreaded();

    const std::string server_address = "localhost:" + server.getPort().toString();
    std::cout << "Server address: " << server_address << "\n";

    Http::Client client;
    client.init();

    auto rb = client.get(server_address);
    auto response = rb.send();
    bool done = false;
    response.then([&](Http::Response rsp)
                  {
                      if (rsp.code() == Http::Code::Ok)
                          done = true;
                  }, Async::IgnoreException);

    Async::Barrier<Http::Response> barrier(response);
    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_TRUE(done);
}

TEST(http_client_test, one_client_with_multiple_requests) {
    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.serveThreaded();

    const std::string server_address = "localhost:" + server.getPort().toString();
    std::cout << "Server address: " << server_address << "\n";

    Http::Client client;
    client.init();

    std::vector<Async::Promise<Http::Response>> responses;
    const int RESPONSE_SIZE = 3;
    int response_counter = 0;

    auto rb = client.get(server_address);
    for (int i = 0; i < RESPONSE_SIZE; ++i) {
        auto response = rb.send();
        response.then([&](Http::Response rsp) {
            if (rsp.code() == Http::Code::Ok)
                ++response_counter;
        }, Async::IgnoreException);
        responses.push_back(std::move(response));
    }

    auto sync = Async::whenAll(responses.begin(), responses.end());
    Async::Barrier<std::vector<Http::Response>> barrier(sync);

    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_TRUE(response_counter == RESPONSE_SIZE);
}

TEST(http_client_test, multiple_clients_with_one_request) {
    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.serveThreaded();

    const std::string server_address = "localhost:" + server.getPort().toString();
    std::cout << "Server address: " << server_address << "\n";

    const int CLIENT_SIZE = 3;
    Http::Client client1;
    client1.init();
    Http::Client client2;
    client2.init();
    Http::Client client3;
    client3.init();

    std::vector<Async::Promise<Http::Response>> responses;
    std::atomic<int> response_counter(0);

    auto rb1 = client1.get(server_address);
    auto response1 = rb1.send();
    response1.then([&](Http::Response rsp) {
            if (rsp.code() == Http::Code::Ok)
                ++response_counter;
        }, Async::IgnoreException);
    responses.push_back(std::move(response1));
    auto rb2 = client2.get(server_address);
    auto response2 = rb2.send();
    response2.then([&](Http::Response rsp) {
            if (rsp.code() == Http::Code::Ok)
                ++response_counter;
        }, Async::IgnoreException);
    responses.push_back(std::move(response2));
    auto rb3 = client3.get(server_address);
    auto response3 = rb3.send();
    response3.then([&](Http::Response rsp) {
            if (rsp.code() == Http::Code::Ok)
                ++response_counter;
        }, Async::IgnoreException);
    responses.push_back(std::move(response3));

    auto sync = Async::whenAll(responses.begin(), responses.end());
    Async::Barrier<std::vector<Http::Response>> barrier(sync);

    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client1.shutdown();
    client2.shutdown();
    client3.shutdown();

    ASSERT_TRUE(response_counter == CLIENT_SIZE);
}

TEST(http_client_test, timeout_reject)
{
    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<DelayHandler>());
    server.serveThreaded();

    const std::string server_address = "localhost:" + server.getPort().toString();
    std::cout << "Server address: " << server_address << "\n";

    Http::Client client;
    client.init();

    auto rb = client.get(server_address).timeout(std::chrono::milliseconds(1000));
    auto response = rb.send();
    bool is_reject = false;
    response.then([&is_reject](Http::Response /*rsp*/)
                  {
                      is_reject = false;
                  },
                  [&is_reject](std::exception_ptr /*exc*/)
                  {
                      is_reject = true;  
                  });

    Async::Barrier<Http::Response> barrier(response);
    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_TRUE(is_reject);
}

TEST(http_client_test, one_client_with_multiple_requests_and_one_connection_per_host_and_two_threads)
{
    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.serveThreaded();

    const std::string server_address = "localhost:" + server.getPort().toString();
    std::cout << "Server address: " << server_address << "\n";

    Http::Client client;
    auto opts = Http::Client::options().maxConnectionsPerHost(1).threads(2);
    client.init(opts);

    std::vector<Async::Promise<Http::Response>> responses;
    const int RESPONSE_SIZE = 6;
    std::atomic<int> response_counter(0);

    auto rb = client.get(server_address);
    for (int i = 0; i < RESPONSE_SIZE; ++i)
    {
        auto response = rb.send();
        response.then([&](Http::Response rsp)
                      {
                          if (rsp.code() == Http::Code::Ok)
                              ++response_counter;
                      },
                      Async::IgnoreException);
        responses.push_back(std::move(response));
    }

    auto sync = Async::whenAll(responses.begin(), responses.end());
    Async::Barrier<std::vector<Http::Response>> barrier(sync);

    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_TRUE(response_counter == RESPONSE_SIZE);
}

TEST(http_client_test, one_client_with_multiple_requests_and_two_connections_per_host_and_one_thread)
{
    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.serveThreaded();

    const std::string server_address = "localhost:" + server.getPort().toString();
    std::cout << "Server address: " << server_address << "\n";

    Http::Client client;
    auto opts = Http::Client::options().maxConnectionsPerHost(2).threads(1);
    client.init(opts);

    std::vector<Async::Promise<Http::Response>> responses;
    const int RESPONSE_SIZE = 6;
    std::atomic<int> response_counter(0);

    auto rb = client.get(server_address);
    for (int i = 0; i < RESPONSE_SIZE; ++i)
    {
        auto response = rb.send();
        response.then([&](Http::Response rsp)
                      {
                          if (rsp.code() == Http::Code::Ok)
                              ++response_counter;
                      },
                      Async::IgnoreException);
        responses.push_back(std::move(response));
    }

    auto sync = Async::whenAll(responses.begin(), responses.end());
    Async::Barrier<std::vector<Http::Response>> barrier(sync);

    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_TRUE(response_counter == RESPONSE_SIZE);
}
