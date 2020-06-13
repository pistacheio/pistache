/*
   Mathieu Stefani, 07 février 2016

   Example of a REST endpoint with routing
*/

#include "gtest/gtest.h"

#include <pistache/endpoint.h>
#include <pistache/http.h>
#include <pistache/peer.h>
#include <pistache/router.h>

#include "httplib.h"

using namespace std;
using namespace Pistache;

class StatsEndpoint {
public:
  StatsEndpoint(Address addr)
      : httpEndpoint(std::make_shared<Http::Endpoint>(addr)) {}

  void init(size_t thr = 2) {
    auto opts = Http::Endpoint::options().threads(static_cast<int>(thr));
    httpEndpoint->init(opts);
    setupRoutes();
  }

  void start() {
    httpEndpoint->setHandler(router.handler());
    httpEndpoint->serveThreaded();
  }

  void shutdown() { httpEndpoint->shutdown(); }

  Port getPort() const { return httpEndpoint->getPort(); }

private:
  void setupRoutes() {
    using namespace Rest;
    Routes::Get(router, "/read/function1",
                Routes::bind(&StatsEndpoint::doAuth, this));
    Routes::Get(router, "/read/hostname",
                Routes::bind(&StatsEndpoint::doResolveClient, this));
  }

  void doAuth(const Rest::Request & /*request*/,
              Http::ResponseWriter response) {
    std::thread worker(
        [](Http::ResponseWriter writer) { writer.send(Http::Code::Ok, "1"); },
        std::move(response));
    worker.detach();
  }

  void doResolveClient(const Rest::Request & /*request*/,
                       Http::ResponseWriter response) {
    response.send(Http::Code::Ok, response.peer()->hostname());
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

  res = client.Get("/read/hostname");
  ASSERT_EQ(res->status, 200);

  // TODO: Clean this up to use proper gtest macros.
  // NOTE: res->body is "ip6-localhost" on some architectures.
  if (res->body == "ip6-localhost") {
    ASSERT_EQ(res->body, "ip6-localhost");  // count the passing test.
  } else {
    ASSERT_EQ(res->body, "localhost");
  }
  stats.shutdown();
}

TEST(rest_server_test, response_status_code_test) {
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

  // Code 404 - Not Found.
  auto res = client.Get("/read/does_not_exist");
  EXPECT_EQ(res->status, 404);
  EXPECT_EQ(res->body, "Could not find a matching route");

  // Code 405 - Method Not Allowed.
  std::string body("body goes here");
  res = client.Post("/read/function1", body, "text/plain");
  EXPECT_EQ(res->status, 405);
  EXPECT_EQ(res->body, "Method Not Allowed");
  ASSERT_TRUE(res->has_header("Allow"));
  EXPECT_EQ(res->get_header_value("Allow"), "GET");

  // Code 415 - Unknown Media Type
  res = client.Post("/read/function1", body, "invalid");
  EXPECT_EQ(res->status, 415);
  EXPECT_EQ(res->body, "Unknown Media Type");

  stats.shutdown();
}
