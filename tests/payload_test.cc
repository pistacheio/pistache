#include "gtest/gtest.h"

#include <pistache/http.h>
#include <pistache/description.h>
#include <pistache/client.h>
#include <pistache/endpoint.h>

using namespace std;
using namespace Pistache;

struct TestParameter {
    size_t      payloadBytes;
    Http::Code  expectedResponseCode;
};

typedef std::vector<TestParameter> PayloadTestSizes;
 
void testPayloads(std::string url, PayloadTestSizes & testPayloads) {
   // Client tests to make sure the payload is enforced
    Http::Client client;
    auto client_opts = Http::Client::options().threads(1).maxConnectionsPerHost(1);
    client.init(client_opts);

    std::vector<Async::Promise<Http::Response>> responses;

    size_t tests_passed = 0;
    for (auto & t : testPayloads) {
        auto rb = client.post(url);
        std::string payload(t.payloadBytes, 'A');
        auto response = rb.body(payload).send();
        response.then([t,&tests_passed](Http::Response rsp) {
                if (rsp.code() == t.expectedResponseCode)
                    ++tests_passed;
                }, Async::IgnoreException);
        responses.push_back(std::move(response));
    }

    auto sync = Async::whenAll(responses.begin(), responses.end());
    Async::Barrier<std::vector<Http::Response>> barrier(sync);
    barrier.wait_for(std::chrono::seconds(5));
    client.shutdown();

    ASSERT_TRUE(tests_passed == testPayloads.size());
}

void handleEcho(const Rest::Request&req, Http::ResponseWriter response) {
    response.send(Http::Code::Ok, req.body(), MIME(Text, Plain));
}

TEST(payload_test, from_description)
{
    Address addr(Ipv4::any(), 9084);
    const size_t threads = 2;
    const size_t max_payload = 1024; // very small

    std::shared_ptr<Http::Endpoint> endpoint;
    Rest::Description desc("Rest Description Test", "v1");
    Rest::Router router;

    desc
        .route(desc.post("/echo"))
        .bind(&handleEcho)
        .response(Http::Code::Ok, "Response to the /ready call");

    router.initFromDescription(desc);
    router.setMaxPayload(max_payload);

    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;

    auto opts = Http::Endpoint::options()
        .threads(threads)
        .flags(flags);

    endpoint = std::make_shared<Pistache::Http::Endpoint>(addr);
    endpoint->init(opts);
    endpoint->setHandler(router.handler());

    ASSERT_EQ(router.handler()->getMaxPayload(), max_payload);
    endpoint->serveThreaded();

    PayloadTestSizes payloads{
       {1024, Http::Code::Ok}
        ,{2048, Http::Code::Request_Entity_Too_Large}
    };

    testPayloads("127.0.0.1:9084/echo", payloads);
    endpoint->shutdown();
}

TEST(payload_test, manual_construction) {
    class MyHandler : public Http::Handler {
        HTTP_PROTOTYPE(MyHandler)


        void onRequest(
                const Http::Request& req,
                Http::ResponseWriter response)
        {
            if (req.resource() == "/echo") {
                if (req.method() == Http::Method::Post) {
                    response.send(Http::Code::Ok, req.body(), MIME(Text, Plain));
                } else {
                    response.send(Http::Code::Method_Not_Allowed);
                }
            } else {
                response.send(Http::Code::Not_Found);
            }

        }
    private:
        tag garbage;
        
    };

    Port port(9080);
    int thr = 2;
    Address addr(Ipv4::any(), port);
    auto server = std::make_shared<Http::Endpoint>(addr);
    size_t maxPayload = 2048;

    auto opts = Http::Endpoint::options()
        .threads(thr)
        .flags(Tcp::Options::InstallSignalHandler)
        .maxPayload(maxPayload);
    server->init(opts);
    server->setHandler(Http::make_handler<MyHandler>());
    server->serveThreaded();

    PayloadTestSizes payloads{
       {1024, Http::Code::Ok}
       , {1800, Http::Code::Ok}
       , {2048, Http::Code::Request_Entity_Too_Large}
    };

    testPayloads("127.0.0.1:9080/echo", payloads);

}


