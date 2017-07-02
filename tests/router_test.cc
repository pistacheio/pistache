/* router_test.cc
   Mathieu Stefani, 06 janvier 2016
   
   Unit tests for the rest router
*/


#include "gtest/gtest.h"
#include <algorithm>

#include <pistache/router.h>

using namespace Pistache;

bool match(const Rest::Route& route, const std::string& req) {
    return std::get<0>(route.match(req));
}

bool matchParams(
        const Rest::Route& route, const std::string& req,
        std::initializer_list<std::pair<std::string, std::string>> list)
{
    bool ok;
    std::vector<Rest::TypedParam> params;
    std::tie(ok, params, std::ignore) = route.match(req);

    if (!ok) return false;

    for (const auto& p: list) {
        auto it = std::find_if(params.begin(), params.end(), [&](const Rest::TypedParam& param) {
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

bool matchSplat(
        const Rest::Route& route, const std::string& req,
        std::initializer_list<std::string> list)
{
    bool ok;
    std::vector<Rest::TypedParam> splats;
    std::tie(ok, std::ignore, splats) = route.match(req);
    
    if (!ok) return false;

    if (list.size() != splats.size()) {
        std::cerr << "Size mismatch (" << list.size() << " != " << splats.size() << ")"
                  << std::endl;
        return false;
    }

    size_t i = 0;
    for (const auto& s: list) {
        auto splat = splats[i].as<std::string>();
        if (splat != s) {
            std::cerr << "Splat number " << i << " did not match ("
                      << splat << " != " << s << ")" << std::endl;
            return false;
        }
        ++i;
    }

    return true;
}

Rest::Route
makeRoute(std::string value) {
    auto noop = [](const Http::Request&, Http::Response) { return Rest::Route::Result::Ok; };
    return Rest::Route(value, Http::Method::Get, noop);
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
    ASSERT_TRUE(matchParams(r1, "/v1/hello/joe", {
            { ":name", "joe" }
    }));

    auto r2 = makeRoute("/greetings/:from/:to");
    ASSERT_TRUE(matchParams(r2, "/greetings/foo/bar", {
            { ":from", "foo" },
            { ":to"   , "bar" }
    }));
}

TEST(router_test, test_optional) {
    auto r1 = makeRoute("/get/:key?");
    ASSERT_TRUE(match(r1, "/get"));
    ASSERT_TRUE(match(r1, "/get/"));
    ASSERT_TRUE(matchParams(r1, "/get/foo", {
            { ":key", "foo" }
    }));
    ASSERT_TRUE(matchParams(r1, "/get/foo/", {
            { ":key", "foo" }
    }));

    ASSERT_FALSE(match(r1, "/get/foo/bar"));
}

TEST(router_test, test_splat) {
    auto r1 = makeRoute("/say/*/to/*");
    ASSERT_TRUE(match(r1, "/say/hello/to/user"));
    ASSERT_FALSE(match(r1, "/say/hello/to"));
    ASSERT_FALSE(match(r1, "/say/hello/to/user/please"));

    ASSERT_TRUE(matchSplat(r1, "/say/hello/to/user", { "hello", "user" }));
    ASSERT_TRUE(matchSplat(r1, "/say/hello/to/user/", { "hello", "user" }));
}
