/* router.h
   Mathieu Stefani, 05 janvier 2016
   
   Simple HTTP Rest Router
*/

#pragma once

#include <string>
#include "http.h"
#include "http_defs.h"

namespace Net {

namespace Rest {

class TypedParam {
public:
    TypedParam(const std::string& name, const std::string& value)
        : name_(name)
        , value_(value)
    { }

    template<typename T>
    T as() const {
        /* @FixMe: use some sort of lexical casting here */
        return value_;
    }

    std::string name() const {
        return name_;
    }

private:
    std::string name_;
    std::string value_;
};

class Request : public Http::Request {
public:
    explicit Request(
            const Http::Request& request, 
            std::vector<TypedParam>&& params);

    TypedParam param(std::string name) const;

private:

    std::unordered_map<std::string, TypedParam> params_;
};

class Router {
public:
    typedef std::function<void (const Request&, Net::Http::Response)> Handler;

    struct Route {
        Route(std::string resource, Http::Method method, Handler handler)
            : resource_(std::move(resource))
            , method_(method)
            , handler_(std::move(handler))
            , parts_(splitUrl(resource_))
        {
        }

        std::pair<bool, std::vector<TypedParam>>
        match(const Http::Request& req) const;

        template<typename... Args>
        void invokeHandler(Args&& ...args) const {
            handler_(std::forward<Args>(args)...);
        }

    private:
        std::vector<std::string> splitUrl(const std::string& resource) const;

        std::string resource_;
        Net::Http::Method method_;
        Handler handler_;
        /* @Performance: since we know that resource_ will live as long as the vector underneath,
         * we would benefit from std::experimental::string_view to store parts of the resource.
         *
         * We could use string_view instead of allocating strings everytime. However, string_view is
         * only available in c++17, so I might have to come with my own lightweight implementation of
         * it
         */
        std::vector<std::string> parts_;
    };

    class HttpHandler : public Net::Http::Handler {
    public:
        HttpHandler(const std::unordered_map<Http::Method, std::vector<Route>>& routes);

        void onRequest(
                const Http::Request& req,
                Http::Response response,
                Http::Timeout timeout);

    private:
        std::unordered_map<Http::Method, std::vector<Route>> routes;
    };

    std::shared_ptr<HttpHandler>
    handler() const {
        return std::make_shared<HttpHandler>(routes);
    }

    void get(std::string resource, Handler handler);
    void post(std::string resource, Handler handler);
    void put(std::string resource, Handler handler);
    void del(std::string resource, Handler handler);

private:
    void addRoute(Http::Method method, std::string resource, Handler handler);
    std::unordered_map<Http::Method, std::vector<Route>> routes;
};

namespace Routes {

    void Get(Router& router, std::string resource, Router::Handler handler);
    void Post(Router& router, std::string resource, Router::Handler handler);
    void Put(Router& router, std::string resource, Router::Handler handler);
    void Delete(Router& router, std::string resource, Router::Handler handler);

    template<typename Handler, typename Obj>
    void Get(Router& router, std::string resource, Handler handler, Obj obj) {
        Get(router, std::move(resource), std::bind(handler, obj, std::placeholders::_1, std::placeholders::_2));
    }

    template<typename Handler, typename Obj>
    void Post(Router& router, std::string resource, Handler handler, Obj obj) {
        Post(router, std::move(resource), std::bind(handler, obj, std::placeholders::_1, std::placeholders::_2));
    }

} // namespace Routing

} // namespace Rest

} // namespace Net
