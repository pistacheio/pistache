/* router.cc
   Mathieu Stefani, 05 janvier 2016
   
   Rest routing implementation
*/

#include "router.h"
#include <algorithm>

namespace Net {

namespace Rest {

Request::Request(
        const Http::Request& request,
        std::vector<TypedParam>&& params)
    : Http::Request(request)
    , params_(std::move(params))
{
}

bool
Request::hasParam(std::string name) const {
    auto it = std::find_if(params_.begin(), params_.end(), [&](const TypedParam& param) {
            return param.name() == name;
    });

    return it != std::end(params_);
}

TypedParam
Request::param(std::string name) const {
    auto it = std::find_if(params_.begin(), params_.end(), [&](const TypedParam& param) {
            return param.name() == name;
    });

    if (it == std::end(params_)) {
        throw std::runtime_error("Unknown parameter");
    }

    return *it;
}

Router::Route::Fragment::Fragment(std::string value)
{
    if (value.empty())
        throw std::runtime_error("Invalid empty fragment");

    init(std::move(value));
}

bool
Router::Route::Fragment::match(const std::string& raw) const {
    if (flags.hasFlag(Flag::Fixed)) {
        if (!flags.hasFlag(Flag::Optional)) {
            return raw == value_;
        }
    }
    else if (flags.hasFlag(Flag::Parameter)) {
        return true;
    }

    return false;
}

bool
Router::Route::Fragment::match(const Fragment& other) const {
    return match(other.value());
}

void
Router::Route::Fragment::init(std::string value) {
    if (value[0] == ':')
        flags.setFlag(Flag::Parameter);
    else
        flags.setFlag(Flag::Fixed);

    // Let's search for any '?'
    auto pos = value.find('?');
    if (pos != std::string::npos) {
        if (value[0] != ':')
            throw std::runtime_error("Only optional parameters are currently supported");

        if (pos != value.size() - 1)
            throw std::runtime_error("? should be at the end of the string");

        value_ = value.substr(0, pos);
        flags.setFlag(Flag::Optional);
    } else {
        value_ = std::move(value);
    }

    checkInvariant();
}

void
Router::Route::Fragment::checkInvariant() const {
    auto check = [this](std::initializer_list<Flag> exclusiveFlags) {
        for (auto flag: exclusiveFlags) {
            if (!flags.hasFlag(flag)) return;
        }

        throw std::logic_error(
                std::string("Invariant violated: invalid combination of flags for fragment ") + value_);
    };

    check({ Flag::Fixed, Flag::Optional });
    check({ Flag::Fixed, Flag::Parameter });
}

std::vector<Router::Route::Fragment>
Router::Route::Fragment::fromUrl(const std::string& url) {
    std::vector<Router::Route::Fragment> fragments;

    std::istringstream iss(url);
    std::string p;

    while (std::getline(iss, p, '/')) {
        if (p.empty()) continue;

        fragments.push_back(Fragment(std::move(p)));
    }

    return fragments;
}

bool
Router::Route::Fragment::isParameter() const {
    return flags.hasFlag(Flag::Parameter);
}

bool
Router::Route::Fragment::isOptional() const {
    return isParameter() && flags.hasFlag(Flag::Optional);
}

std::pair<bool, std::vector<TypedParam>>
Router::Route::match(const Http::Request& req) const
{
    return match(req.resource());
}

std::pair<bool, std::vector<TypedParam>>
Router::Route::match(const std::string& req) const
{
    auto reqFragments = Fragment::fromUrl(req);
    if (reqFragments.size() > fragments_.size())
        return std::make_pair(false, std::vector<TypedParam>());

    std::vector<TypedParam> extractedParams;

    for (std::vector<Fragment>::size_type i = 0; i < fragments_.size(); ++i) {
        const auto& fragment = fragments_[i];
        if (i >= reqFragments.size()) {
            if (fragment.isOptional())
                continue;

            return std::make_pair(false, std::vector<TypedParam>());
        }

        const auto& reqFragment = reqFragments[i];
        if (!fragment.match(reqFragment))
            return std::make_pair(false, std::vector<TypedParam>());

        if (fragment.isParameter()) {
            extractedParams.push_back(TypedParam(fragment.value(), reqFragment.value()));
        }

    }

    return make_pair(true, std::move(extractedParams));
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
            return;
        }
    }

    response.send(Http::Code::Not_Found, "Could not find a matching route");
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
