/* router_test.cc
   Mathieu Stefani, 06 janvier 2016

   Unit tests for the rest router
*/

#include "gtest/gtest.h"
#include <algorithm>

#include <pistache/endpoint.h>
#include <pistache/http.h>
#include <pistache/router.h>

#include "httplib.h"

using namespace Pistache;
using namespace Pistache::Rest;

bool match(const SegmentTreeNode &routes, const std::string &req) {
  const auto &s = SegmentTreeNode::sanitizeResource(req);
  std::shared_ptr<Route> route;
  std::tie(route, std::ignore, std::ignore) =
      routes.findRoute({s.data(), s.size()});
  return route != nullptr;
}

bool matchParams(
    const SegmentTreeNode &routes, const std::string &req,
    std::initializer_list<std::pair<std::string, std::string>> list) {

  const auto &s = SegmentTreeNode::sanitizeResource(req);
  std::shared_ptr<Route> route;
  std::vector<TypedParam> params;
  std::string_view sv{s.data(), s.length()};
  std::tie(route, params, std::ignore) = routes.findRoute(sv);

  if (route == nullptr)
    return false;

  for (const auto &p : list) {
    auto it = std::find_if(
        params.begin(), params.end(),
        [&](const TypedParam &param) { return param.name() == p.first; });
    if (it == std::end(params))
      return false;
    if (it->as<std::string>() != p.second)
      return false;
  }
  return true;
}

bool matchSplat(const SegmentTreeNode &routes, const std::string &req,
                std::initializer_list<std::string> list) {

  const auto &s = SegmentTreeNode::sanitizeResource(req);
  std::shared_ptr<Route> route;
  std::vector<TypedParam> splats;
  std::string_view sv{s.data(), s.length()};
  std::tie(route, std::ignore, splats) = routes.findRoute(sv);

  if (route == nullptr)
    return false;

  if (list.size() != splats.size())
    return false;

  size_t i = 0;
  for (const auto &s : list) {
    auto splat = splats[i].as<std::string>();
    if (splat != s)
      return false;
    ++i;
  }

  return true;
}

TEST(router_test, test_fixed_routes) {
  SegmentTreeNode routes;
  auto s = SegmentTreeNode::sanitizeResource("/v1/hello");
  routes.addRoute(std::string_view{s.data(), s.length()}, nullptr, nullptr);

  ASSERT_TRUE(match(routes, "/v1/hello"));
  ASSERT_FALSE(match(routes, "/v2/hello"));
  ASSERT_FALSE(match(routes, "/v1/hell0"));

  s = SegmentTreeNode::sanitizeResource("/a/b/c");
  routes.addRoute(std::string_view{s.data(), s.length()}, nullptr, nullptr);
  ASSERT_TRUE(match(routes, "/a/b/c"));
}

TEST(router_test, test_parameters) {
  SegmentTreeNode routes;
  const auto &s = SegmentTreeNode::sanitizeResource("/v1/hello/:name/");
  routes.addRoute(std::string_view{s.data(), s.length()}, nullptr, nullptr);

  ASSERT_TRUE(matchParams(routes, "/v1/hello/joe", {{":name", "joe"}}));

  const auto &p = SegmentTreeNode::sanitizeResource("/greetings/:from/:to");
  routes.addRoute(std::string_view{p.data(), p.length()}, nullptr, nullptr);
  ASSERT_TRUE(matchParams(routes, "/greetings/foo/bar",
                          {{":from", "foo"}, {":to", "bar"}}));
}

TEST(router_test, test_optional) {
  SegmentTreeNode routes;
  auto s = SegmentTreeNode::sanitizeResource("/get/:key?/bar");
  routes.addRoute(std::string_view{s.data(), s.length()}, nullptr, nullptr);

  ASSERT_FALSE(matchParams(routes, "/get/bar", {{":key", "whatever"}}));
  ASSERT_TRUE(matchParams(routes, "/get/foo/bar", {{":key", "foo"}}));
}

TEST(router_test, test_splat) {
  SegmentTreeNode routes;
  auto s = SegmentTreeNode::sanitizeResource("/say/*/to/*");
  routes.addRoute(std::string_view{s.data(), s.length()}, nullptr, nullptr);

  ASSERT_TRUE(match(routes, "/say/hello/to/user"));
  ASSERT_FALSE(match(routes, "/say/hello/to"));
  ASSERT_FALSE(match(routes, "/say/hello/to/user/please"));

  ASSERT_TRUE(matchSplat(routes, "/say/hello/to/user", {"hello", "user"}));
  ASSERT_TRUE(matchSplat(routes, "/say/hello/to/user/", {"hello", "user"}));
}

TEST(router_test, test_sanitize) {
  SegmentTreeNode routes;
  auto s = SegmentTreeNode::sanitizeResource("//v1//hello/");
  routes.addRoute(std::string_view{s.data(), s.length()}, nullptr, nullptr);

  ASSERT_TRUE(match(routes, "/v1/hello////"));
}

TEST(router_test, test_mixed) {
  SegmentTreeNode routes;
  auto s = SegmentTreeNode::sanitizeResource("/hello");
  auto p = SegmentTreeNode::sanitizeResource("/*");
  routes.addRoute(std::string_view{s.data(), s.length()}, nullptr, nullptr);
  routes.addRoute(std::string_view{p.data(), p.length()}, nullptr, nullptr);

  ASSERT_TRUE(match(routes, "/hello"));
  ASSERT_TRUE(match(routes, "/hi"));

  ASSERT_FALSE(matchSplat(routes, "/hello", {"hello"}));
  ASSERT_TRUE(matchSplat(routes, "/hi", {"hi"}));
}

TEST(router_test, test_notfound_exactly_once) {
  Address addr(Ipv4::any(), 0);
  auto endpoint = std::make_shared<Http::Endpoint>(addr);

  auto opts = Http::Endpoint::options().threads(1).maxRequestSize(4096);
  endpoint->init(opts);

  int count_found = 0;
  int count_not_found = 0;

  Rest::Router router;
  Routes::NotFound(router, [&count_not_found](
                               const Pistache::Rest::Request &request,
                               Pistache::Http::ResponseWriter response) {
    count_not_found++;
    std::string err{"Couldn't find route: \"" + request.resource() + "\"\n"};
    response.send(Pistache::Http::Code::Not_Found, err);
    return Pistache::Rest::Route::Result::Ok;
  });
  Routes::Get(router, "/moogle",
              [&count_found](const Pistache::Rest::Request &,
                             Pistache::Http::ResponseWriter response) {
                count_found++;
                response.send(Pistache::Http::Code::Ok, "kupo!\n");
                return Pistache::Rest::Route::Result::Ok;
              });

  endpoint->setHandler(router.handler());
  endpoint->serveThreaded();
  const auto bound_port = endpoint->getPort();
  httplib::Client client("localhost", bound_port);

  // Verify that the notFound handler is NOT called when route is found.
  count_not_found = count_found = 0;
  client.Get("/moogle");
  ASSERT_EQ(count_found, 1);
  ASSERT_EQ(count_not_found, 0);

  // Verify simple solution to bug #323 (one bad url triggered 2 routes).
  count_not_found = count_found = 0;
  client.Get("/kefka");
  ASSERT_EQ(count_found, 0);
  ASSERT_EQ(count_not_found, 1);

  // Anal test, 2 calls = 2 route hits.
  count_not_found = count_found = 0;
  client.Get("/vicks");
  client.Get("/wedge");
  ASSERT_EQ(count_found, 0);
  ASSERT_EQ(count_not_found, 2);

  endpoint->shutdown();
}

TEST(router_test, test_route_head_request) {
  Address addr(Ipv4::any(), 0);
  auto endpoint = std::make_shared<Http::Endpoint>(addr);

  auto opts = Http::Endpoint::options().threads(1).maxRequestSize(4096);
  endpoint->init(opts);

  int count_found = 0;

  Rest::Router router;

  Routes::Head(router, "/moogle",
               [&count_found](const Pistache::Rest::Request &,
                              Pistache::Http::ResponseWriter response) {
                 count_found++;
                 response.send(Pistache::Http::Code::Ok);
                 return Pistache::Rest::Route::Result::Ok;
               });

  endpoint->setHandler(router.handler());
  endpoint->serveThreaded();
  const auto bound_port = endpoint->getPort();
  httplib::Client client("localhost", bound_port);

  count_found = 0;
  client.Head("/moogle");
  ASSERT_EQ(count_found, 1);

  endpoint->shutdown();
}
