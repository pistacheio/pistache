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

    Port getPort() const {
        return httpEndpoint->getPort();
    }

private:
    void setupRoutes() {
        using namespace Rest;
        Routes::Get(router, "/read/function1", Routes::bind(&StatsEndpoint::doAuth, this));
    }

    void doAuth(const Rest::Request& /*request*/, Http::ResponseWriter response) {
        std::thread worker([](Http::ResponseWriter writer) {
            writer.send(Http::Code::Ok, "1");
        }, std::move(response));
        worker.detach();
    }

    std::shared_ptr<Http::Endpoint> httpEndpoint;
    Rest::Router router;
};

TEST(rest_server_test, basic_test) {
    int thr = 1;

    Address addr(Ipv4::any(), Port(0));

    StatsEndpoint stats(addr);

    stats.init(thr);
    stats.start();
    Port port = stats.getPort();

    cout << "Cores = " << hardware_concurrency() << endl;
    cout << "Using " << thr << " threads" << endl;
    cout << "Port = " << port << endl;

    httplib::Client client("localhost", port);
    auto res = client.Get("/read/function1");
    ASSERT_EQ(res->status, 200);
    ASSERT_EQ(res->body, "1");

    stats.shutdown();
}
