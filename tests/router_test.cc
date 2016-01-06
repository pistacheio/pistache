/* router_test.cc
   Mathieu Stefani, 06 janvier 2016
   
   Unit tests for the rest router
*/


#include "gtest/gtest.h"
#include "router.h"
#include <algorithm>

using namespace Net::Rest;

bool match(const Router::Route& route, const std::string& req) {
    return route.match(req).first;
}

bool match(
        const Router::Route& route, const std::string& req,
        std::initializer_list<std::pair<std::string, std::string>> list)
{
    bool ok;
    std::vector<TypedParam> params;
    std::tie(ok, params) = route.match(req);

    if (!ok) return false;

    for (const auto& p: list) {
        auto it = std::find_if(params.begin(), params.end(), [&](const TypedParam& param) {
            return param.name() == p.first;
        });
        if (it == std::end(params)) {
            std::cerr << "Did not find param '" << p.first << "'" << std::endl;
            return false;
        }
        if (it->as<std::string>() != p.second) {
            std::cerr << "Param '" << p.first << "' mismatched ("
                      << p.second << " != " << it->as<std::string>() << ")" << std::endl;
            return false;
        }

    }
    return true;
}



Router::Route
makeRoute(std::string value) {
    auto noop = [](const Net::Http::Request&, Net::Http::Response) { };
    return Router::Route(value, Net::Http::Method::Get, noop);
}

TEST(router_test, test_fixed_routes) {
    auto r1 = makeRoute("/v1/hello");
    ASSERT_TRUE(match(r1, "/v1/hello"));
    ASSERT_FALSE(match(r1, "/v2/hello"));
    ASSERT_FALSE(match(r1, "/v1/hell0"));

    auto r2 = makeRoute("/a/b/c");
    ASSERT_TRUE(match(r2, "/a/b/c"));
}

TEST(router_test, test_parameters) {
    auto r1 = makeRoute("/v1/hello/:name");
    ASSERT_TRUE(match(r1, "/v1/hello/joe", {
            { ":name", "joe" }
    }));

    auto r2 = makeRoute("/greetings/:from/:to");
    ASSERT_TRUE(match(r2, "/greetings/foo/bar", {
            { ":from", "foo" },
            { ":to"   , "bar" }
    }));
}

TEST(router_test, test_optional) {
    auto r1 = makeRoute("/get/:key?");
    ASSERT_TRUE(match(r1, "/get"));
    ASSERT_TRUE(match(r1, "/get/"));
    ASSERT_TRUE(match(r1, "/get/foo", {
            { ":key", "foo" }
    }));
    ASSERT_TRUE(match(r1, "/get/foo/", {
            { ":key", "foo" }
    }));

    ASSERT_FALSE(match(r1, "/get/foo/bar"));
}
