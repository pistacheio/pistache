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
Request::hasParam(std::string name) const {
    auto it = std::find_if(params_.begin(), params_.end(), [&](const TypedParam& param) {
            return param.name() == std::string_view(name.data(), name.length());
    });

    return it != std::end(params_);
}

TypedParam
Request::param(std::string name) const {
    auto it = std::find_if(params_.begin(), params_.end(), [&](const TypedParam& param) {
            return param.name() == std::string_view(name.data(), name.length());
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
    if (result == Route::Status::NotFound)
        response.send(Http::Code::Not_Found, "Could not find a matching route");
}

} // namespace Private

FragmentTreeNode::FragmentTreeNode(): resourceRef_(), fixed_(), param_(), optional_(), splat_(), route_() {
    std::shared_ptr<char> ptr(new char[1], std::default_delete<char[]>());
    ptr.get()[0] = '/';
    resourceRef_.swap(ptr);
}

FragmentTreeNode::FragmentTreeNode(const std::shared_ptr<char> &resourceReference) :
        resourceRef_(resourceReference), fixed_(), param_(), optional_(), splat_(), route_() { }

FragmentTreeNode::FragmentType
FragmentTreeNode::getFragmentType(const std::string_view &fragment) {
    // Let's search for any '?'
    auto pos = fragment.find('?');
    if (pos != std::string_view::npos) {
        if (fragment[0] != ':') {
            throw std::runtime_error("Only optional parameters are currently supported");
        }
        if (pos != fragment.length() - 1) {
            throw std::runtime_error("? should be at the end of the string");
        }
        return FragmentType::Optional;
    }

    pos = fragment.find('*');
    if (pos != std::string_view::npos && pos != 0) {
        throw std::runtime_error("Invalid splat parameter");
    }
    if (fragment[0] == ':') {
        return FragmentType::Param;
    } else if (fragment[0] == '*') {
        if (fragment.length() > 1) {
            throw std::runtime_error("Invalid splat parameter");
        }
        return FragmentType::Splat;
    }
    return FragmentType::Fixed;
}

void
FragmentTreeNode::addRoute(const std::string_view &path,
                           Route::Handler &handler,
                           std::shared_ptr<char> &resourceReference) {
    if (path.length() == 0) {
        throw std::runtime_error("Invalid zero-length URL.");
    }
    std::string_view currPath;
    if (path.rfind('/') == path.length() - 1) {
        currPath = path.substr(0, path.length() - 1);
    } else {
        currPath = path;
    }
    if (currPath.find('/') == std::string_view::npos) { // current leaf requested
        if (route_ != nullptr) {
            throw std::runtime_error("Requested route already exist.");
        } else {
            route_ = std::make_shared<Route>(handler);
        }
    } else { // recursion to correct descendant
        const auto pos = currPath.find('/', 1);
        const auto next = (pos == std::string_view::npos) ? currPath.substr(1) : currPath.substr(pos); // complete lower path
        auto mid = (pos == std::string_view::npos) ? currPath.substr(1) : currPath.substr(1, currPath.find('/', pos) - 1); // middle resource name

        std::unordered_map<std::string_view, std::shared_ptr<FragmentTreeNode>> *collection;
        const auto fragmentType = getFragmentType(mid);
        switch (fragmentType) {
            case FragmentType::Fixed:
                collection = &fixed_;
                break;
            case FragmentType::Param:
                collection = &param_;
                break;
            case FragmentType::Optional:
                mid = mid.substr(0, mid.length() - 1);
                collection = &optional_;
                break;
            case FragmentType::Splat:
                if (splat_ == nullptr) {
                    splat_ = std::make_shared<FragmentTreeNode>(resourceReference);
                }
                splat_->addRoute(next, handler, resourceReference);
                return;
        }


        if (collection->count(mid) == 0) { // direct subpath does not exist
            collection->insert(std::make_pair(mid, std::make_shared<FragmentTreeNode>(resourceReference)));
        }
        collection->at(mid)->addRoute(next, handler, resourceReference);
    }
}

bool Pistache::Rest::FragmentTreeNode::removeRoute(const std::string_view &path) {
    if (path.length() == 0) {
        throw std::runtime_error("Invalid zero-length URL.");
    }
    std::string_view currPath;
    if (path.rfind('/') == path.length() - 1) {
        currPath = path.substr(0, path.length());
    } else {
        currPath = path;
    }
    if (currPath.find('/') == std::string_view::npos) { // current leaf requested
        route_.reset();
    } else { // recursion to correct descendant
        const auto pos = currPath.find('/', 1);
        const auto next = (pos == std::string_view::npos) ? currPath.substr(1) : currPath.substr(pos); // complete lower path
        auto mid = (pos == std::string_view::npos) ? currPath.substr(1) : currPath.substr(1, currPath.find('/', pos) - 1); // middle resource name
        std::unordered_map<std::string_view, std::shared_ptr<FragmentTreeNode>> *collection;

        auto fragmentType = getFragmentType(mid);
        switch (fragmentType) {
            case FragmentType::Fixed:
                collection = &fixed_;
                break;
            case FragmentType::Param:
                collection = &param_;
                break;
            case FragmentType::Optional:
                mid = mid.substr(0, mid.length() - 1);
                collection = &optional_;
                break;
            case FragmentType::Splat:
                return splat_->removeRoute(next);
        }

        try {
            const bool removable = collection->at(mid)->removeRoute(next);
            if (removable) {
                collection->erase(mid);
            }
        } catch (const std::out_of_range &) {
            throw std::runtime_error("Requested does not exist.");
        }
    }
    return fixed_.empty() && param_.empty() &&
           optional_.empty() && splat_ == nullptr && route_ == nullptr;
}

Route::Status Pistache::Rest::FragmentTreeNode::invokeRouteHandler(
        const std::string_view &path, const Http::Request &req,
        Http::ResponseWriter response,
        std::vector<TypedParam> &params,
        std::vector<TypedParam> &splats) const {
    if (path.length() == 0) {
        throw std::runtime_error("Invalid zero-length URL.");
    }
    std::string_view currPath;
    if (path.rfind('/') == path.length() - 1) {
        currPath = path.substr(0, path.length() - 1);
    } else {
        currPath = path;
    }
    if (currPath.find('/') == std::string_view::npos) { // current leaf requested
        route_->invokeHandler(Request(req, std::move(params), std::move(splats)), std::move(response));
        return Route::Status::Match;
    } else { // recursion to correct descendant
        const auto pos = currPath.find('/', 1);
        const auto next = (pos == std::string_view::npos) ? currPath.substr(1) : currPath.substr(pos); // complete lower path
        auto mid = (pos == std::string_view::npos) ? currPath.substr(1) : currPath.substr(1, currPath.find('/', pos) - 1); // middle resource name

        if (fixed_.count(mid) != 0) {
            try {
                return fixed_.at(mid)->invokeRouteHandler(next, req, std::move(response), params, splats);
            } catch (std::runtime_error &) {

            }
        }

        for (const auto &param: param_) {
            try {
                params.push_back(TypedParam(param.first, mid));
                return param.second->invokeRouteHandler(next, req, std::move(response), params, splats);
            } catch (std::runtime_error &) {
                params.pop_back();
            }

        }

        for (const auto &optional: optional_) {
            try {
                return optional.second->invokeRouteHandler(next, req, std::move(response), params, splats);
            }
            catch (std::runtime_error &) {}
        }

        try {
            if (splat_!= nullptr) {
                splats.push_back(TypedParam(mid, mid));
                return splat_->invokeRouteHandler(next, req, std::move(response), params, splats);
            }
        } catch (std::runtime_error &) {
            splats.pop_back();
        }
        throw std::runtime_error("Cannot find route on given leaf.");
    }
}

Route::Status
Pistache::Rest::FragmentTreeNode::invokeRouteHandler(const Http::Request& req, Http::ResponseWriter response) const {
    std::vector<TypedParam> params;
    std::vector<TypedParam> splats;
    std::string_view path = {req.resource().data(), req.resource().length()};
    try {
        return invokeRouteHandler(path, req, std::move(response), params, splats);
    } catch (std::runtime_error&) {
        return Route::Status::NotFound;

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
Router::removeRoute(Http::Method method, std::string resource) {
    auto& r = routes[method];
    r.removeRoute(std::string_view(resource.data(), resource.length()));
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
    try {
        return r.invokeRouteHandler(req, std::move(response));
    } catch (std::runtime_error&) {
        for (const auto &handler: customHandlers) {
            auto resp = response.clone();
            auto result = handler(Request(req, std::vector<TypedParam>(), std::vector<TypedParam>()), std::move(resp));
            if (result == Route::Result::Ok) return Route::Status::Match;
        }

        return Route::Status::NotFound;
    }
}

void
Router::addRoute(Http::Method method, std::string resource, Route::Handler handler)
{
    auto& r = routes[method];
    std::shared_ptr<char> ptr(new char[resource.length()], std::default_delete<char[]>());
    memcpy(ptr.get(), resource.data(), resource.length());
    r.addRoute(std::string_view(ptr.get(), resource.length()), handler, ptr);
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


void Remove(Router& router, Http::Method method, std::string resource) {
    router.removeRoute(method, resource);

void NotFound(Router& router, Route::Handler handler) {
    router.addNotFoundHandler(std::move(handler));
}

} // namespace Routes
} // namespace Rest
} // namespace Pistache
