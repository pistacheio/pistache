/* router_test.cc
   Mathieu Stefani, 06 janvier 2016
   
   Unit tests for the rest router
*/


#include "gtest/gtest.h"
#include <algorithm>

#include <pistache/router.h>

using namespace Pistache;

bool match(const Rest::FragmentTreeNode& routes, const std::string& req) {
    std::shared_ptr<Rest::Route> route;
    std::tie(route, std::ignore, std::ignore) = routes.findRoute({req.data(), req.size()});
    return route != nullptr;
}

bool matchParams(
    const Rest::FragmentTreeNode& routes, const std::string& req,
    std::initializer_list<std::pair<std::string, std::string>> list)
{
    std::shared_ptr<Rest::Route> route;
    std::vector<Rest::TypedParam> params;
    std::tie(route, params, std::ignore) = routes.findRoute({req.data(), req.size()});

    if (route == nullptr) return false;

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
    const Rest::FragmentTreeNode& routes, const std::string& req,
    std::initializer_list<std::string> list)
{
    std::shared_ptr<Rest::Route> route;
    std::vector<Rest::TypedParam> splats;
    std::tie(route, std::ignore, splats) = routes.findRoute({req.data(), req.size()});

    if (route == nullptr) return false;

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

TEST(router_test, test_fixed_routes) {
    Rest::FragmentTreeNode routes;
    routes.addRoute(std::string_view("/v1/hello"), nullptr, nullptr);

    ASSERT_TRUE(match(routes, "/v1/hello"));
    ASSERT_FALSE(match(routes, "/v2/hello"));
    ASSERT_FALSE(match(routes, "/v1/hell0"));

    routes.addRoute(std::string_view("/a/b/c"), nullptr, nullptr);
    ASSERT_TRUE(match(routes, "/a/b/c"));
}

TEST(router_test, test_parameters) {
    Rest::FragmentTreeNode routes;
    routes.addRoute(std::string_view("/v1/hello/:name"), nullptr, nullptr);

    ASSERT_TRUE(matchParams(routes, "/v1/hello/joe", {
        { ":name", "joe" }
    }));

    routes.addRoute(std::string_view("/greetings/:from/:to"), nullptr, nullptr);
    ASSERT_TRUE(matchParams(routes, "/greetings/foo/bar", {
        { ":from", "foo" },
        { ":to"   , "bar" }
    }));
}

TEST(router_test, test_optional) {
    Rest::FragmentTreeNode routes;
    routes.addRoute(std::string_view("/get/:key?"), nullptr, nullptr);

    ASSERT_TRUE(match(routes, "/get"));
    ASSERT_TRUE(match(routes, "/get/"));
    ASSERT_TRUE(matchParams(routes, "/get/foo", {
        { ":key", "foo" }
    }));
    ASSERT_TRUE(matchParams(routes, "/get/foo/", {
        { ":key", "foo" }
    }));

    ASSERT_FALSE(match(routes, "/get/foo/bar"));
}

TEST(router_test, test_splat) {
    Rest::FragmentTreeNode routes;
    routes.addRoute(std::string_view("/say/*/to/*"), nullptr, nullptr);

    ASSERT_TRUE(match(routes, "/say/hello/to/user"));
    ASSERT_FALSE(match(routes, "/say/hello/to"));
    ASSERT_FALSE(match(routes, "/say/hello/to/user/please"));

    ASSERT_TRUE(matchSplat(routes, "/say/hello/to/user", { "hello", "user" }));
    ASSERT_TRUE(matchSplat(routes, "/say/hello/to/user/", { "hello", "user" }));
}