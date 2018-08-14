/* router.h
   Mathieu Stefani, 05 janvier 2016
   
   Simple HTTP Rest Router
*/

#pragma once

#include <string>
#include <tuple>
#include <unordered_map>
#include <memory>

#include <pistache/http.h>
#include <pistache/http_defs.h>
#include <pistache/flags.h>

#include "pistache/string_view.h"

namespace Pistache {
namespace Rest {

class Description;

namespace details {
    template<typename T> struct LexicalCast {
        static T cast(const std::string_view& value) {
            std::istringstream iss(std::string(value.data(), value.length()));
            T out;
            if (!(iss >> out))
                throw std::runtime_error("Bad lexical cast");
            return out;
        }
    };

    template<>
    struct LexicalCast<std::string_view> {
        static std::string_view cast(const std::string_view& value) {
            return value;
        }
    };
}

class TypedParam {
public:
    TypedParam(const std::string_view& name, const std::string_view& value)
        : name_(name)
        , value_(value)
    { }

    template<typename T>
    T as() const {
        return details::LexicalCast<T>::cast(value_);
    }

    std::string_view name() const {
        return name_;
    }

private:
    std::string_view name_;
    std::string_view value_;
};

class Request;

struct Route {

    enum class Status { Match, NotFound };

    enum class Result {
        Ok, Failure
    };

    typedef std::function<Result(const Request, Http::ResponseWriter)> Handler;

    explicit Route(Route::Handler handler) : handler_(std::move(handler)) { }

    template<typename... Args>
    void invokeHandler(Args &&...args) const {
        handler_(std::forward<Args>(args)...);
    }

    Handler handler_;
};



namespace Private {
    class RouterHandler;
}

class FragmentTreeNode {
private:
    enum class FragmentType {
        Fixed, Param, Optional, Splat
    };

    /**
     * Resource path are allocated on stack. To make them survive it is required to allocate them on heap.
     * Since the main idea between string_view is to have constant substring and thus a reduced number of
     * char* allocated on heap, this shared_ptr is used to know if a string_view can be destroyed (after a
     * route removal).
     */
    std::shared_ptr<char> resourceRef_;

    std::unordered_map<std::string_view, std::shared_ptr<FragmentTreeNode>> fixed_;
    std::unordered_map<std::string_view, std::shared_ptr<FragmentTreeNode>> param_;
    std::unordered_map<std::string_view, std::shared_ptr<FragmentTreeNode>> optional_;
    std::shared_ptr<FragmentTreeNode> splat_;
    std::shared_ptr<Route> route_;

    static FragmentType getFragmentType(const std::string_view &fragment);

    Route::Status invokeRouteHandler(const std::string_view &path,
                                     const Http::Request& req, Http::ResponseWriter response,
                                     std::vector<TypedParam> &params,
                                     std::vector<TypedParam> &splats) const;

public:
    FragmentTreeNode();
    explicit FragmentTreeNode(const std::shared_ptr<char> &resourceReference);

    void addRoute(const std::string_view &path, Route::Handler &handler, std::shared_ptr<char> &resourceReference);

    bool removeRoute(const std::string_view &path);

    Route::Status invokeRouteHandler(const Http::Request& req, Http::ResponseWriter response) const;
};

class Router {
public:
    static Router fromDescription(const Rest::Description& desc);

    std::shared_ptr<Private::RouterHandler>
    handler() const;

    void initFromDescription(const Rest::Description& desc);

    void get(std::string resource, Route::Handler handler);
    void post(std::string resource, Route::Handler handler);
    void put(std::string resource, Route::Handler handler);
    void patch(std::string resource, Route::Handler handler);
    void del(std::string resource, Route::Handler handler);
    void options(std::string resource, Route::Handler handler);
    void removeRoute(Http::Method method, std::string resource);

    void addCustomHandler(Route::Handler handler);
    void addNotFoundHandler(Route::Handler handler);
    inline bool hasNotFoundHandler() { return notFoundHandler != nullptr; }
    void invokeNotFoundHandler(const Http::Request &req, Http::ResponseWriter resp) const;

    Route::Status route(const Http::Request& request, Http::ResponseWriter response);    

    Status route(const Http::Request& request, Http::ResponseWriter response);

private:
    void addRoute(Http::Method method, std::string resource, Route::Handler handler);
    std::unordered_map<Http::Method, FragmentTreeNode> routes;

    std::vector<Route::Handler> customHandlers;

    Route::Handler notFoundHandler;
};

namespace Private {

    class RouterHandler : public Http::Handler {
    public:
        RouterHandler(const Rest::Router& router);

        void onRequest(
                const Http::Request& req,
                Http::ResponseWriter response);

    private:
        std::shared_ptr<Tcp::Handler> clone() const {
            return std::make_shared<RouterHandler>(router);
        }

        Rest::Router router;
    };
}

class Request : public Http::Request {
public:
    friend class FragmentTreeNode;
    friend class Router;

    bool hasParam(std::string name) const;
    TypedParam param(std::string name) const;

    TypedParam splatAt(size_t index) const;
    std::vector<TypedParam> splat() const;

private:
    explicit Request(
            const Http::Request& request,
            std::vector<TypedParam>&& params,
            std::vector<TypedParam>&& splats);

    std::vector<TypedParam> params_;
    std::vector<TypedParam> splats_;
};


namespace Routes {

    void Get(Router& router, std::string resource, Route::Handler handler);
    void Post(Router& router, std::string resource, Route::Handler handler);
    void Put(Router& router, std::string resource, Route::Handler handler);
    void Patch(Router& router, std::string resource, Route::Handler handler);
    void Delete(Router& router, std::string resource, Route::Handler handler);
    void Options(Router& router, std::string resource, Route::Handler handler);
    void Remove(Router& router, Http::Method method, std::string resource);

    void NotFound(Router& router, Route::Handler handler);

    namespace details {
        template <class... Args>
        struct TypeList
        {
            template<size_t N>
            struct At {
                static_assert(N < sizeof...(Args), "Invalid index");
                typedef typename std::tuple_element<N, std::tuple<Args...>>::type Type;
            };
        };

        template<typename... Args>
        void static_checks() {
            static_assert(sizeof...(Args) == 2, "Function should take 2 parameters");
//            typedef details::TypeList<Args...> Arguments;
            // Disabled now as it
            // 1/ does not compile
            // 2/ might not be relevant
#if 0
            static_assert(std::is_same<Arguments::At<0>::Type, const Rest::Request&>::value, "First argument should be a const Rest::Request&");
            static_assert(std::is_same<typename Arguments::At<0>::Type, Http::Response>::value, "Second argument should be a Http::Response");
#endif
        }
    }


    template<typename Result, typename Cls, typename... Args, typename Obj>
    Route::Handler bind(Result (Cls::*func)(Args...), Obj obj) {
        details::static_checks<Args...>();

        #define CALL_MEMBER_FN(obj, pmf)  ((obj)->*(pmf))

        return [=](const Rest::Request& request, Http::ResponseWriter response) {
            CALL_MEMBER_FN(obj, func)(request, std::move(response));

            return Route::Result::Ok;
        };

        #undef CALL_MEMBER_FN
    }

    template<typename Result, typename... Args>
    Route::Handler bind(Result (*func)(Args...)) {
        details::static_checks<Args...>();

        return [=](const Rest::Request& request, Http::ResponseWriter response) {
            func(request, std::move(response));

            return Route::Result::Ok;
        };
    }

} // namespace Routing
} // namespace Rest
} // namespace Pistache
