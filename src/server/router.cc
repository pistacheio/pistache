/* router.cc
   Mathieu Stefani, 05 janvier 2016

   Rest routing implementation
*/

#include <algorithm>

#include <pistache/router.h>
#include <pistache/description.h>

namespace Pistache {
namespace Rest {

Request::Request(
        const Http::Request& request,
        std::vector<TypedParam>&& params,
        std::vector<TypedParam>&& splats)
    : Http::Request(request)
    , params_(std::move(params))
    , splats_(std::move(splats))
{
}

bool
Request::hasParam(const std::string& name) const {
    auto it = std::find_if(params_.begin(), params_.end(), [&](const TypedParam& param) {
            return param.name() == name;
    });

    return it != std::end(params_);
}

TypedParam
Request::param(const std::string& name) const {
    auto it = std::find_if(params_.begin(), params_.end(), [&](const TypedParam& param) {
            return param.name() == name;
    });

    if (it == std::end(params_)) {
        throw std::runtime_error("Unknown parameter");
    }

    return *it;
}

TypedParam
Request::splatAt(size_t index) const {
    if (index >= splats_.size()) {
        throw std::out_of_range("Request splat index out of range");
    }

    return splats_[index];
}

std::vector<TypedParam>
Request::splat() const {
    return splats_;
}

Route::Fragment::Fragment(std::string value)
{
    if (value.empty())
        throw std::runtime_error("Invalid empty fragment");

    init(std::move(value));
}

bool
Route::Fragment::match(const std::string& raw) const {
    if (flags.hasFlag(Flag::Fixed)) {
        return raw == value_;
    }
    else if (flags.hasFlag(Flag::Parameter) || flags.hasFlag(Flag::Splat)) {
        return true;
    }

    return false;
}

bool
Route::Fragment::match(const Fragment& other) const {
    return match(other.value());
}

void
Route::Fragment::init(std::string value) {
    if (value[0] == ':')
        flags.setFlag(Flag::Parameter);
    else if (value[0] == '*') {
        if (value.size() > 1)
            throw std::runtime_error("Invalid splat parameter");
        flags.setFlag(Flag::Splat);
    }
    else {
        flags.setFlag(Flag::Fixed);
    }

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
Route::Fragment::checkInvariant() const {
    auto check = [this](std::initializer_list<Flag> exclusiveFlags) {
        for (auto flag: exclusiveFlags) {
            if (!flags.hasFlag(flag)) return;
        }

        throw std::logic_error(
                std::string("Invariant violated: invalid combination of flags for fragment ") + value_);
    };

    check({ Flag::Fixed, Flag::Optional });
    check({ Flag::Fixed, Flag::Parameter });
    check({ Flag::Splat, Flag::Fixed });
    check({ Flag::Splat, Flag::Optional });
    check({ Flag::Splat, Flag::Parameter });
}

std::vector<Route::Fragment>
Route::Fragment::fromUrl(const std::string& url) {
    std::vector<Route::Fragment> fragments;

    std::istringstream iss(url);
    std::string p;

    while (std::getline(iss, p, '/')) {
        if (p.empty()) continue;

        fragments.push_back(Fragment(std::move(p)));
    }

    return fragments;
}

bool
Route::Fragment::isParameter() const {
    return flags.hasFlag(Flag::Parameter);
}

bool
Route::Fragment::isOptional() const {
    return isParameter() && flags.hasFlag(Flag::Optional);
}

bool
Route::Fragment::isSplat() const {
    return flags.hasFlag(Flag::Splat);
}

std::tuple<bool, std::vector<TypedParam>, std::vector<TypedParam>>
Route::match(const Http::Request& req) const
{
    return match(req.resource());
}

std::tuple<bool, std::vector<TypedParam>, std::vector<TypedParam>>
Route::match(const std::string& req) const
{
    static const auto NoMatch = std::make_tuple(false, std::vector<TypedParam>(), std::vector<TypedParam>());

    auto reqFragments = Fragment::fromUrl(req);
    if (reqFragments.size() > fragments_.size())
        return NoMatch;

    std::vector<TypedParam> params;
    std::vector<TypedParam> splats;

    for (std::vector<Fragment>::size_type i = 0; i < fragments_.size(); ++i) {
        const auto& fragment = fragments_[i];
        if (i >= reqFragments.size()) {
            if (fragment.isOptional())
                continue;

            return NoMatch;
        }

        const auto& reqFragment = reqFragments[i];
        if (!fragment.match(reqFragment))
            return NoMatch;

        if (fragment.isParameter()) {
            params.push_back(TypedParam(fragment.value(), reqFragment.value()));
        } else if (fragment.isSplat()) {
            splats.push_back(TypedParam(reqFragment.value(), reqFragment.value()));
        }

    }

    return make_tuple(true, std::move(params), std::move(splats));
}

namespace Private {

RouterHandler::RouterHandler(const Rest::Router& router)
    : router(router)
{
}

void
RouterHandler::onRequest(
        const Http::Request& req,
        Http::ResponseWriter response)
{
    auto resp = response.clone();
    auto result = router.route(req, std::move(resp));

    /* @Feature: add support for a custom NotFound handler */
    if (result == Router::Status::NotFound)
    {
        if (router.hasNotFoundHandler())
        {
            auto resp2 = response.clone();
            router.invokeNotFoundHandler(req, std::move(resp2));
        }
        else
            response.send(Http::Code::Not_Found, "Could not find a matching route");
    }
}

} // namespace Private

Router
Router::fromDescription(const Rest::Description& desc) {
    Router router;
    router.initFromDescription(desc);
    return router;
}

std::shared_ptr<Private::RouterHandler>
Router::handler() const {
    return std::make_shared<Private::RouterHandler>(*this);
}

void
Router::initFromDescription(const Rest::Description& desc) {
    auto paths = desc.rawPaths();
    for (auto it = paths.flatBegin(), end = paths.flatEnd(); it != end; ++it) {
        const auto& paths = *it;
        for (const auto& path: paths) {
            if (!path.isBound()) {
                std::ostringstream oss;
                oss << "Path '" << path.value << "' is not bound";
                throw std::runtime_error(oss.str());
            }
            addRoute(path.method, std::move(path.value), std::move(path.handler));
        }
    }
}

void
Router::get(std::string resource, Route::Handler handler) {
    addRoute(Http::Method::Get, std::move(resource), std::move(handler));
}

void
Router::post(std::string resource, Route::Handler handler) {
    addRoute(Http::Method::Post, std::move(resource), std::move(handler));
}

void
Router::put(std::string resource, Route::Handler handler) {
    addRoute(Http::Method::Put, std::move(resource), std::move(handler));
}

void
Router::patch(std::string resource, Route::Handler handler) {
    addRoute(Http::Method::Patch, std::move(resource), std::move(handler));
}

void
Router::del(std::string resource, Route::Handler handler) {
    addRoute(Http::Method::Delete, std::move(resource), std::move(handler));
}

void
Router::options(std::string resource, Route::Handler handler) {
    addRoute(Http::Method::Options, std::move(resource), std::move(handler));
}

void
Router::addCustomHandler(Route::Handler handler) {
    customHandlers.push_back(std::move(handler));
}

void
Router::addNotFoundHandler(Route::Handler handler) {
    notFoundHandler = std::move(handler);
}

void
Router::invokeNotFoundHandler(const Http::Request &req, Http::ResponseWriter resp) const
{
    notFoundHandler(Rest::Request(std::move(req), std::vector<TypedParam>(), std::vector<TypedParam>()), std::move(resp));
}

Router::Status
Router::route(const Http::Request& req, Http::ResponseWriter response) {
    auto& r = routes[req.method()];
    for (const auto& route: r) {
        bool match;
        std::vector<TypedParam> params;
        std::vector<TypedParam> splats;
        std::tie(match, params, splats) = route.match(req);
        if (match) {
            route.invokeHandler(Request(req, std::move(params), std::move(splats)), std::move(response));
            return Router::Status::Match;
        }
    }

    for (const auto& handler: customHandlers) {
        auto resp = response.clone();
        auto result = handler(Request(req, std::vector<TypedParam>(), std::vector<TypedParam>()), std::move(resp));
        if (result == Route::Result::Ok) return Router::Status::Match;
    }

    return Router::Status::NotFound;
}

void
Router::addRoute(Http::Method method, std::string resource, Route::Handler handler)
{
    auto& r = routes[method];
    r.push_back(Route(std::move(resource), method, std::move(handler)));
}


namespace Routes {

void Get(Router& router, std::string resource, Route::Handler handler) {
    router.get(std::move(resource), std::move(handler));
}

void Post(Router& router, std::string resource, Route::Handler handler) {
    router.post(std::move(resource), std::move(handler));
}

void Put(Router& router, std::string resource, Route::Handler handler) {
    router.put(std::move(resource), std::move(handler));
}

void Patch(Router& router, std::string resource, Route::Handler handler) {
    router.patch(std::move(resource), std::move(handler));
}

void Delete(Router& router, std::string resource, Route::Handler handler) {
    router.del(std::move(resource), std::move(handler));
}

void Options(Router& router, std::string resource, Route::Handler handler) {
    router.options(std::move(resource), std::move(handler));
}

void NotFound(Router& router, Route::Handler handler) {
    router.addNotFoundHandler(std::move(handler));
}

} // namespace Routes
} // namespace Rest
} // namespace Pistache
