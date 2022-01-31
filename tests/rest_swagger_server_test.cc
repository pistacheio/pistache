#include <gtest/gtest.h>

#include <pistache/description.h>
#include <pistache/endpoint.h>
#include <pistache/http.h>
#include <pistache/peer.h>
#include <pistache/router.h>
#include <pistache/serializer/rapidjson.h>

#include <filesystem>
#include <httplib.h>

using namespace std;
using namespace Pistache;

class SwaggerEndpoint
{
public:
    SwaggerEndpoint(Address addr)
        : httpEndpoint(std::make_shared<Http::Endpoint>(addr))
        , desc("SwaggerEndpoint API", "1.0")
    { }

    void init()
    {
        auto opts = Http::Endpoint::options().threads(1);
        httpEndpoint->init(opts);
    }

    void start()
    {
        router.initFromDescription(desc);

        Rest::Swagger swagger(desc);
        swagger
            .uiPath("/doc")
            .uiDirectory(filesystem::current_path() / "assets")
            .apiPath("/banker-api.json")
            .serializer(&Rest::Serializer::rapidJson)
            .install(router);

        httpEndpoint->setHandler(router.handler());
        httpEndpoint->serve();
    }

    void shutdown() { httpEndpoint->shutdown(); }

    Port getPort() const { return httpEndpoint->getPort(); }

private:
    shared_ptr<Http::Endpoint> httpEndpoint;
    Rest::Description desc;
    Rest::Router router;
};

TEST(rest_server_test, basic_test)
{
    filesystem::create_directory("assets");

    ofstream goodFile("assets/good.txt");
    goodFile << "good";
    goodFile.close();

    ofstream badFile("bad.txt");
    badFile << "bad";
    badFile.close();

    Address addr(Ipv4::any(), Port(0));
    SwaggerEndpoint swagger(addr);

    swagger.init();
    std::thread t(std::bind(&SwaggerEndpoint::start, swagger));

    while(swagger.getPort() == 0);

    Port port = swagger.getPort();

    cout << "Cores = " << hardware_concurrency() << endl;
    cout << "CWD = " << filesystem::current_path() << endl;
    cout << "Port = " << port << endl;

    httplib::Client client("localhost", port);

    // Test if we have access to files inside the UI folder.
    auto goodRes = client.Get("/doc/good.txt");
    ASSERT_EQ(goodRes->status, 200);
    ASSERT_EQ(goodRes->body, "good");

    // Attempt to read file outside of the UI directory should fail even if
    // the file exists.
    auto badRes = client.Get("/doc/../bad.txt");
    ASSERT_EQ(badRes->status, 404);
    ASSERT_NE(badRes->body, "bad");

    swagger.shutdown();
    t.join();
    filesystem::remove_all("assets");
    filesystem::remove_all("bad.txt");
}
