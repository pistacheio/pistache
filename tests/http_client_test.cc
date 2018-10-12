#include <pistache/http.h>
#include <pistache/client.h>
#include <pistache/endpoint.h>

#include "gtest/gtest.h"

#include <chrono>

using namespace Pistache;

struct HelloHandler : public Http::Handler {
    HTTP_PROTOTYPE(HelloHandler)

    void onRequest(const Http::Request& /*request*/, Http::ResponseWriter writer)
    {
        writer.send(Http::Code::Ok, "Hello, World!");
    }
};

TEST(http_client_test, one_client_with_one_request) {
    const std::string address = "localhost:9079";

    Http::Endpoint server(address);
    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.serveThreaded();

    Http::Client client;
    client.init();

    std::vector<Async::Promise<Http::Response>> responses;
    auto rb = client.get(address);
    auto response = rb.send();
    bool done = false;
    response.then([&](Http::Response rsp) {
        if (rsp.code() == Http::Code::Ok)
            done = true;
        }, Async::IgnoreException);
    responses.push_back(std::move(response));

    auto sync = Async::whenAll(responses.begin(), responses.end());
    Async::Barrier<std::vector<Http::Response>> barrier(sync);
    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_TRUE(done);
}

TEST(http_client_test, one_client_with_multiple_requests) {
    const std::string address = "localhost:9080";

    Http::Endpoint server(address);
    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.serveThreaded();

    Http::Client client;
    client.init();

    std::vector<Async::Promise<Http::Response>> responses;
    const int RESPONSE_SIZE = 3;
    int response_counter = 0;

    auto rb = client.get(address);
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
    const std::string address = "localhost:9081";

    Http::Endpoint server(address);
    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.serveThreaded();

    const int CLIENT_SIZE = 3;
    Http::Client client1;
    client1.init();
    Http::Client client2;
    client2.init();
    Http::Client client3;
    client3.init();

    std::vector<Async::Promise<Http::Response>> responses;
    int response_counter = 0;

    auto rb1 = client1.get(address);
    auto response1 = rb1.send();
    response1.then([&](Http::Response rsp) {
            if (rsp.code() == Http::Code::Ok)
                ++response_counter;
        }, Async::IgnoreException);
    responses.push_back(std::move(response1));
    auto rb2 = client2.get(address);
    auto response2 = rb2.send();
    response2.then([&](Http::Response rsp) {
            if (rsp.code() == Http::Code::Ok)
                ++response_counter;
        }, Async::IgnoreException);
    responses.push_back(std::move(response2));
    auto rb3 = client3.get(address);
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