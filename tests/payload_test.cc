#include "gtest/gtest.h"

#include <pistache/http.h>
#include <pistache/description.h>
#include <pistache/client.h>
#include <pistache/endpoint.h>

using namespace std;
using namespace Pistache;

struct TestSet {
    TestSet()
        : bytes(0)
          , expectedCode(Http::Code::Ok)
          , actualCode(Http::Code::Ok)
    { }

    TestSet(size_t b, Http::Code c)
        : bytes(b)
          , expectedCode(c)
          , actualCode(Http::Code::Ok)
    { }

    size_t      bytes;
    Http::Code  expectedCode;
    Http::Code  actualCode;
};

typedef std::vector<TestSet> PayloadTestSets;
 
void testPayloads(std::string url, PayloadTestSets & testPayloads) {
   // Client tests to make sure the payload is enforced
    std::mutex resultsetMutex;
    PayloadTestSets test_results;
    Http::Client client;
    auto client_opts = Http::Client::options()
                         .threads(1)
                         .maxConnectionsPerHost(1);
    client.init(client_opts);
    auto rb = client.post(url);
    for (auto & t : testPayloads) {
        std::string payload(t.bytes, 'A');
        std::vector<Async::Promise<Http::Response> > responses;
        auto response = rb.body(payload).send();
        response.then([t,&test_results,&resultsetMutex](Http::Response rsp) {
                TestSet res(t);
                res.actualCode = rsp.code();
                {
                    std::unique_lock<std::mutex> lock(resultsetMutex);
                    test_results.push_back(res);
                }
                }, Async::IgnoreException);
        responses.push_back(std::move(response));
        auto sync = Async::whenAll(responses.begin(), responses.end());
        Async::Barrier<std::vector<Http::Response>> barrier(sync);
        barrier.wait_for(std::chrono::seconds(5));
    }
    client.shutdown();

    for (auto & result : test_results) {
        ASSERT_EQ(result.expectedCode, result.actualCode);
    }
}

void handleEcho(const Rest::Request&req, Http::ResponseWriter response) {
    response.send(Http::Code::Ok, req.body(), MIME(Text, Plain));
}

TEST(payload_test, from_description)
{
    Address addr(Ipv4::any(), 9084);
    const size_t threads = 20;
    const size_t maxPayload = 1024; // very small

    std::shared_ptr<Http::Endpoint> endpoint;
    Rest::Description desc("Rest Description Test", "v1");
    Rest::Router router;

    desc
        .route(desc.post("/echo"))
        .bind(&handleEcho)
        .response(Http::Code::Ok, "Response to the /ready call");

    router.initFromDescription(desc);

    auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;

    auto opts = Http::Endpoint::options()
        .threads(threads)
        .flags(flags)
        .maxPayload(maxPayload);
        ;

    endpoint = std::make_shared<Pistache::Http::Endpoint>(addr);
    endpoint->init(opts);
    endpoint->setHandler(router.handler());

    endpoint->serveThreaded();

    PayloadTestSets payloads{
       {800, Http::Code::Ok}
       , {1024, Http::Code::Request_Entity_Too_Large}
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
        tag placeholder;
    };

    Port port(9080);
    int thr = 20;
    Address addr(Ipv4::any(), port);
    auto endpoint = std::make_shared<Http::Endpoint>(addr);
    size_t maxPayload = 2048;

    auto opts = Http::Endpoint::options()
        .threads(thr)
        .flags(Tcp::Options::InstallSignalHandler)
        .maxPayload(maxPayload);

    endpoint->init(opts);
    endpoint->setHandler(Http::make_handler<MyHandler>());
    endpoint->serveThreaded();

    PayloadTestSets payloads{
       {1024, Http::Code::Ok}
       , {1800, Http::Code::Ok}
       , {2048, Http::Code::Request_Entity_Too_Large}
       , {4096, Http::Code::Request_Entity_Too_Large}
    };

    testPayloads("127.0.0.1:9080/echo", payloads);

}


