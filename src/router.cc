/* router.cc
   Mathieu Stefani, 05 janvier 2016
   
   Rest routing implementation
*/

#include "router.h"

namespace Net {

namespace Rest {

Request::Request(
        const Http::Request& request,
        std::vector<TypedParam>&& params)
    : Http::Request(request)
{
    for (auto&& param: params) {
        params_.insert(std::make_pair(param.name(), std::move(param)));
    }
}

TypedParam
Request::param(std::string name) const {
    auto it = params_.find(std::move(name));
    if (it == std::end(params_)) {
        throw std::runtime_error("Unknown parameter");
    }

    return it->second;
}

std::pair<bool, std::vector<TypedParam>>
Router::Route::match(const Http::Request& req) const
{
    auto reqParts = splitUrl(req.resource());
    if (reqParts.size() != parts_.size()) return std::make_pair(false, std::vector<TypedParam>());

    auto isUrlParameter = [](const std::string& str) {
        return str.size() >= 1 && str[0] == ':';
    };

    std::vector<TypedParam> extractedParams;
    for (std::vector<std::string>::size_type i = 0; i < reqParts.size(); ++i) {
        if (!isUrlParameter(parts_[i])) {
            if (reqParts[i] != parts_[i]) return std::make_pair(false, std::vector<TypedParam>());
            continue;
        }

        TypedParam param(parts_[i], reqParts[i]);
        extractedParams.push_back(std::move(param));
    }

    return make_pair(true, std::move(extractedParams));
}

std::vector<std::string>
Router::Route::splitUrl(const std::string& resource) const {
    std::istringstream iss(resource);
    std::string p;
    std::vector<std::string> parts;
    while (std::getline(iss, p, '/')) {
        parts.push_back(std::move(p));
    }

    return parts;
}

Router::HttpHandler::HttpHandler(
        const std::unordered_map<Http::Method, std::vector<Router::Route>>& routes)
    : routes(routes)
{
}

void
Router::HttpHandler::onRequest(
        const Http::Request& req,
        Http::Response response,
        Http::Timeout timeout)
{
    auto& r = routes[req.method()];
    for (const auto& route: r) {
        bool match;
        std::vector<TypedParam> params;
        std::tie(match, params) = route.match(req);
        if (match) {
            route.invokeHandler(Request(req, std::move(params)), std::move(response));
        }
    }
}

void
Router::get(std::string resource, Router::Handler handler) {
    addRoute(Http::Method::Get, std::move(resource), std::move(handler));
}

void
Router::post(std::string resource, Router::Handler handler) {
    addRoute(Http::Method::Post, std::move(resource), std::move(handler));
}

void
Router::put(std::string resource, Router::Handler handler) {
    addRoute(Http::Method::Put, std::move(resource), std::move(handler));
}

void
Router::del(std::string resource, Router::Handler handler) {
    addRoute(Http::Method::Delete, std::move(resource), std::move(handler));
}

void
Router::addRoute(Http::Method method, std::string resource, Router::Handler handler)
{
    auto& r = routes[method];
    r.push_back(Route(std::move(resource), method, std::move(handler)));
}


namespace Routes {

void Get(Router& router, std::string resource, Router::Handler handler) {
    router.get(std::move(resource), std::move(handler));
}

void Post(Router& router, std::string resource, Router::Handler handler) {
    router.post(std::move(resource), std::move(handler));
}

void Put(Router& router, std::string resource, Router::Handler handler) {
    router.put(std::move(resource), std::move(handler));
}

void Delete(Router& router, std::string resource, Router::Handler handler) {
    router.del(std::move(resource), std::move(handler));
}

} // namespace Routing

} // namespace Rest

} // namespace Net
