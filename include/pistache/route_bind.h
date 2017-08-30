/* 
   Mathieu Stefani, 27 février 2016
   
   A special bind() method for REST routes
*/

#pragma once

namespace Pistache {
namespace Rest {
namespace Route {

    void Get(Router& router, std::string resource, Route::Handler handler);
    void Post(Router& router, std::string resource, Route::Handler handler);
    void Put(Router& router, std::string resource, Route::Handler handler);
    void Delete(Router& router, std::string resource, Route::Handler handler);

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
            typedef details::TypeList<Args...> Arguments;
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
        };

        #undef CALL_MEMBER_FN
    }

    template<typename Result, typename... Args>
    Route::Handler bind(Result (*func)(Args...)) {
        details::static_checks<Args...>();

        return [=](const Rest::Request& request, Http::ResponseWriter response) {
            func(request, std::move(response));
        };
    }

} // namespace Route
} // namespace Rest
} // namespace Pistache
