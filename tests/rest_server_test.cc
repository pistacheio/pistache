/* 
   Mathieu Stefani, 07 février 2016
   
   Example of a REST endpoint with routing
*/

#include "gtest/gtest.h"

#include <pistache/http.h>
#include <pistache/router.h>
#include <pistache/endpoint.h>

#include "httplib.h"

using namespace std;
using namespace Pistache;

class StatsEndpoint {
public:
    StatsEndpoint(Address addr)
        : httpEndpoint(std::make_shared<Http::Endpoint>(addr))
    { }

    void init(size_t thr = 2) {
        auto opts = Http::Endpoint::options()
                .threads(thr)
                .flags(Tcp::Options::InstallSignalHandler);
        httpEndpoint->init(opts);
        setupRoutes();
    }

    void start() {
        httpEndpoint->setHandler(router.handler());
        httpEndpoint->serveThreaded();
    }

    void shutdown() {
        httpEndpoint->shutdown();
    }

private:
    void setupRoutes() {
        using namespace Rest;
        Routes::Get(router, "/read/function1", Routes::bind(&StatsEndpoint::doAuth, this));
    }

    void doAuth(const Rest::Request& request, Http::ResponseWriter response) {
        std::thread worker([](Http::ResponseWriter writer) {
            writer.send(Http::Code::Ok, "1");
        }, std::move(response));
        worker.detach();
    }

    std::shared_ptr<Http::Endpoint> httpEndpoint;
    Rest::Router router;
};

TEST(rest_server_test, basic_test) {
    Port port(9090);
    int thr = 1;

    Address addr(Ipv4::any(), port);

    StatsEndpoint stats(addr);

    stats.init(thr);
    stats.start();

    cout << "Cores = " << hardware_concurrency() << endl;
    cout << "Using " << thr << " threads" << endl;

    httplib::Client client("localhost", 9090);
    auto res = client.Get("/read/function1");
    ASSERT_EQ(res->status, 200);
    ASSERT_EQ(res->body, "1");

    stats.shutdown();
}
