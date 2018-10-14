#include <pistache/http.h>
#include <pistache/client.h>
#include <pistache/endpoint.h>

#include "gtest/gtest.h"

#include <chrono>
#include <future>

using namespace Pistache;

struct HelloHandlerWithDelay : public Http::Handler {
    HTTP_PROTOTYPE(HelloHandlerWithDelay)

    explicit HelloHandlerWithDelay(int delay = 0) : delay_(delay)
    { }

    void onRequest(const Http::Request& /*request*/, Http::ResponseWriter writer)
    {
        std::this_thread::sleep_for(std::chrono::seconds(delay_));
        writer.send(Http::Code::Ok, "Hello, World!");
    }

    int delay_;
};

constexpr char SPECIAL_PAGE[] = "/specialpage";

struct SlowHandlerOnSpecialPage : public Http::Handler {
    HTTP_PROTOTYPE(SlowHandlerOnSpecialPage)

    explicit SlowHandlerOnSpecialPage(int delay = 0) : delay_(delay)
    { }

    void onRequest(const Http::Request& request, Http::ResponseWriter writer)
    {
        if (request.resource() == SPECIAL_PAGE)
        {
            std::this_thread::sleep_for(std::chrono::seconds(delay_));
        }
        writer.send(Http::Code::Ok, "Hello, World!");
    }

    int delay_;
};

TEST(http_server_test, client_disconnection_on_timeout_from_single_threaded_server) {
    const std::string address = "localhost:9095";

    Http::Endpoint server(address);
    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    const int SEVEN_SECONDS_DELAY = 6;
    server.setHandler(Http::make_handler<HelloHandlerWithDelay>(SEVEN_SECONDS_DELAY));
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

    ASSERT_FALSE(done);
}

TEST(http_server_test, client_multiple_requests_disconnection_on_timeout_from_single_threaded_server) {
    const std::string address = "localhost:9096";

    Http::Endpoint server(address);
    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    const int SEVEN_SECONDS_DELAY = 6;
    server.setHandler(Http::make_handler<HelloHandlerWithDelay>(SEVEN_SECONDS_DELAY));
    server.serveThreaded();

    Http::Client client;
    client.init();

    std::vector<Async::Promise<Http::Response>> responses;
    auto rb = client.get(address);
    int counter = 0;
    const int RESPONSE_SIZE = 3;
    for (int i = 0; i < RESPONSE_SIZE; ++i)
    {
        auto response = rb.send();
        response.then([&](Http::Response rsp) {
            if (rsp.code() == Http::Code::Ok)
                ++counter;
            }, Async::IgnoreException);
        responses.push_back(std::move(response));
    }

    auto sync = Async::whenAll(responses.begin(), responses.end());
    Async::Barrier<std::vector<Http::Response>> barrier(sync);
    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_EQ(counter, 0);
}

TEST(http_server_test, multiple_client_with_requests_to_multithreaded_server) {
    const std::string address = "localhost:9097";

    Http::Endpoint server(address);
    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags).threads(3);
    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandlerWithDelay>());
    server.serveThreaded();

    auto client_logic = [&address](int response_size) {
        Http::Client client;
        client.init();

        std::vector<Async::Promise<Http::Response>> responses;
        auto rb = client.get(address);
        int counter = 0;
        for (int i = 0; i < response_size; ++i)
        {
            auto response = rb.send();
            response.then([&](Http::Response rsp) {
                if (rsp.code() == Http::Code::Ok)
                    ++counter;
                }, Async::IgnoreException);
            responses.push_back(std::move(response));
        }

        auto sync = Async::whenAll(responses.begin(), responses.end());
        Async::Barrier<std::vector<Http::Response>> barrier(sync);
        barrier.wait_for(std::chrono::seconds(5));

        client.shutdown();

        return counter;
    };

    const int FIRST_CLIENT_REQUEST_SIZE = 4;
    std::future<int> result1(std::async(client_logic, FIRST_CLIENT_REQUEST_SIZE));
    const int SECOND_CLIENT_REQUEST_SIZE = 5;
    std::future<int> result2(std::async(client_logic, SECOND_CLIENT_REQUEST_SIZE));

    int res1 = result1.get();
    int res2 = result2.get();

    server.shutdown();

    ASSERT_EQ(res1, FIRST_CLIENT_REQUEST_SIZE);
    ASSERT_EQ(res2, SECOND_CLIENT_REQUEST_SIZE);
}

TEST(http_server_test, multiple_client_with_different_requests_to_multithreaded_server) {
    const std::string address = "localhost:9098";

    Http::Endpoint server(address);
    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags).threads(3);
    server.init(server_opts);
    const int SEVEN_SECONDS_DELAY = 6;
    server.setHandler(Http::make_handler<SlowHandlerOnSpecialPage>(SEVEN_SECONDS_DELAY));
    server.serveThreaded();

    auto client_logic = [&address](int response_size, const std::string& page) {
        Http::Client client;
        client.init();

        std::vector<Async::Promise<Http::Response>> responses;
        auto rb = client.get(address + page);
        int counter = 0;
        for (int i = 0; i < response_size; ++i)
        {
            auto response = rb.send();
            response.then([&](Http::Response rsp) {
                if (rsp.code() == Http::Code::Ok)
                    ++counter;
                }, Async::IgnoreException);
            responses.push_back(std::move(response));
        }

        auto sync = Async::whenAll(responses.begin(), responses.end());
        Async::Barrier<std::vector<Http::Response>> barrier(sync);
        barrier.wait_for(std::chrono::seconds(5));

        client.shutdown();

        return counter;
    };

    const int FIRST_CLIENT_REQUEST_SIZE = 1;
    std::future<int> result1(std::async(client_logic, FIRST_CLIENT_REQUEST_SIZE, SPECIAL_PAGE));
    const int SECOND_CLIENT_REQUEST_SIZE = 2;
    std::future<int> result2(std::async(client_logic, SECOND_CLIENT_REQUEST_SIZE, ""));

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
}
