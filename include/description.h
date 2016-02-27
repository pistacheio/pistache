/* 
   Mathieu Stefani, 24 février 2016
   
   An API description (reflection) mechanism that is based on Swagger
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <type_traits>
#include <memory>
#include "http_defs.h"
#include "mime.h"
#include "optional.h"
#include "router.h"

namespace Net {

namespace Rest {

namespace Type {

// Data Types

#define DATA_TYPE \
    TYPE(Integer, std::int32_t             , "integer", "int32")  \
    TYPE(Long   , std::int64_t             , "integer", "int64")  \
    TYPE(Float  , float                    , "number" , "float")  \
    TYPE(Double , double                   , "number" , "double") \
    TYPE(String , std::string              , "string" , "")       \
    TYPE(Byte   , char                     , "string" , "byte")   \
    TYPE(Binary , std::vector<std::uint8_t>, "string" , "binary") \
    TYPE(Bool   , bool                     , "boolean", "")       \
    COMPLEX_TYPE(Date    , "string", "date")                      \
    COMPLEX_TYPE(Datetime, "string", "date-time")                 \
    COMPLEX_TYPE(Password, "string", "password")                  \
    COMPLEX_TYPE(Array   , "array",  "array")

#define TYPE(rest, cpp, _, __) \
    typedef cpp rest;
#define COMPLEX_TYPE(rest, _, __) \
    struct rest { };
    DATA_TYPE
#undef TYPE
#undef COMPLEX_TYPE

} // namespace Type

enum class Flag {
    Optional, Required
};

namespace Schema {

namespace Traits {

template<typename DT> struct IsDataType : public std::false_type { };

#define TYPE(rest, _, __, ___) \
    template<> struct IsDataType<Type::rest> : public std::true_type { };
#define COMPLEX_TYPE(rest, _, __) \
    template<> struct IsDataType<Type::rest> : public std::true_type { };
    DATA_TYPE
#undef TYPE
#undef COMPLEX_TYPE

template<typename DT> struct DataTypeInfo;

#define TYPE(rest, _, typeStr, formatStr)                       \
    template<> struct DataTypeInfo<Type::rest> {                \
        static const char *typeName() { return typeStr; } \
        static const char *format() { return formatStr; } \
    };
#define COMPLEX_TYPE(rest, typeStr, formatStr)                 \
    template<> struct DataTypeInfo<Type::rest> {                \
        static const char* typeName() { return typeStr; } \
        static const char* format() { return formatStr; } \
    };
    DATA_TYPE
#undef TYPE
#undef COMPLEX_TYPE

template<typename DT> struct DataTypeValidation {
    static bool validate(const std::string& ) {
        return true;
    }
};

} // namespace Traits

struct Contact {
    Contact(std::string name, std::string url, std::string email);

    std::string name;
    std::string url;
    std::string email;
};

struct License {
    License(std::string name, std::string url);

    std::string name;
    std::string url;
};

struct Info {
    Info(std::string title, std::string version, std::string description = "");

    std::string title;
    std::string version;
    std::string description;
    std::string termsOfService;

    Optional<Contact> contact;
    Optional<License> license;
};

struct InfoBuilder {
    InfoBuilder(Info* info);

    InfoBuilder& termsOfService(std::string value);
    InfoBuilder& contact(std::string name, std::string url, std::string email);
    InfoBuilder& license(std::string name, std::string url);

private:
    Info *info_;
};


struct DataType {
    virtual const char* typeName() const = 0;
    virtual const char* format() const = 0;

    virtual bool validate(const std::string& input) const = 0;
};

template<typename T>
struct DataTypeT : public DataType {
    const char* typeName() const { return Traits::DataTypeInfo<T>::typeName(); }
    const char* format() const { return Traits::DataTypeInfo<T>::format(); }

    bool validate(const std::string& input) const { return Traits::DataTypeValidation<T>::validate(input); }
};

template<typename T>
std::unique_ptr<DataType> makeDataType() {
    static_assert(Traits::IsDataType<T>::value, "Unknown Data Type");
    return std::unique_ptr<DataType>(new DataTypeT<T>());
}

struct Parameter {
    Parameter(
            std::string name, std::string description);

    template<typename T, typename... Args>
    static Parameter create(Args&& ...args) {
        Parameter p(std::forward<Args>(args)...);
        p.type = makeDataType<T>();
        return p;
    }

    std::string name;
    std::string description;
    bool required;
    std::shared_ptr<DataType> type;
};

struct Response {
    Response(Http::Code statusCode, std::string description);

    Http::Code statusCode;
    std::string description;
};

struct ResponseBuilder {
    ResponseBuilder(Http::Code statusCode, std::string description);

    operator Response() const { return response_; }

private:
    Response response_;
};

struct PathFragment {
    PathFragment(std::string value, Http::Method method);

    std::string value;
    Http::Method method;
};

struct Path {
    Path(std::string path, Http::Method method, std::string description);

    std::string value;
    Http::Method method;
    std::string description;

    std::vector<Http::Mime::MediaType> produceMimes;
    std::vector<Http::Mime::MediaType> consumeMimes;
    std::vector<Parameter> parameters;
    std::vector<Response> responses;

    Route::Handler handler;
};

struct PathBuilder {
    PathBuilder(Path* path);

    template<typename... Mimes>
    PathBuilder& produces(Mimes... mimes) {
        const Http::Mime::MediaType m[] = { mimes... };
        std::copy(std::begin(m), std::end(m), std::back_inserter(path_->produceMimes));
        return *this;
    }

    template<typename... Mimes>
    PathBuilder& consumes(Mimes... mimes) {
        const Http::Mime::MediaType m[] = { mimes... };
        std::copy(std::begin(m), std::end(m), std::back_inserter(path_->consumeMimes));
        return *this;
    }

    template<typename T>
    PathBuilder& parameter(std::string name, std::string description) {
        path_->parameters.push_back(Parameter::create<T>(std::move(name), std::move(description)));
        return *this;
    }

    PathBuilder& response(Http::Code statusCode, std::string description) {
        path_->responses.push_back(Response(statusCode, std::move(description)));
        return *this;
    }

    PathBuilder& response(Response response) {
        path_->responses.push_back(std::move(response));
        return *this;
    }

    /* @CodeDup: should re-use Routes::bind */
    template<typename Result, typename Cls, typename... Args, typename Obj>
    PathBuilder& bind(Result (Cls::*func)(Args...), Obj obj) {

        #define CALL_MEMBER_FN(obj, pmf)  ((obj)->*(pmf))

        path_->handler = [=](const Rest::Request& request, Http::ResponseWriter response) {
            CALL_MEMBER_FN(obj, func)(request, std::move(response));
        };

        #undef CALL_MEMBER_FN

        return *this;
    }

    template<typename Result, typename... Args>
    PathBuilder& bind(Result (*func)(Args...)) {

        path_->handler = [=](const Rest::Request& request, Http::ResponseWriter response) {
            func(request, std::move(response));
        };

        return *this;
    }


private:
    Path* path_;
};

struct SubPath {
    SubPath(std::string prefix, std::vector<Path>* paths);

    PathBuilder route(std::string path, Http::Method method, std::string description = "");
    PathBuilder route(PathFragment fragment, std::string description = "");

    SubPath path(std::string prefix);

    template<typename T>
    void parameter(std::string name, std::string description) {
        parameters.push_back(Parameter::create<T>(std::move(name), std::move(description)));
    }

    std::string prefix;
    std::vector<Parameter> parameters;
    std::vector<Path>* paths;
};

} // namespace Schema

class Description {
public:
    Description(std::string title, std::string version, std::string description = "");

    Schema::InfoBuilder info();

    Description& host(std::string value);
    template<typename... Scheme>
    Description& schemes(Scheme... schemes) {
        // @Improve: try to statically assert that every Scheme is convertible to string

        const std::string s[] = { std::string(schemes)... };
        std::copy(std::begin(s), std::end(s), std::back_inserter(schemes_));
        return *this;
    }

    Schema::PathFragment get(std::string name);
    Schema::PathFragment post(std::string name);
    Schema::PathFragment put(std::string name);
    Schema::PathFragment del(std::string name);

    Schema::SubPath path(std::string name);

    Schema::PathBuilder route(std::string name, Http::Method method, std::string description = "");
    Schema::PathBuilder route(Schema::PathFragment fragment, std::string description = "");

    Schema::ResponseBuilder response(Http::Code statusCode, std::string description);

    std::vector<Schema::Path> paths() const { return paths_; }

private:
    Schema::Info info_;
    std::string host_;
    std::vector<std::string> schemes_;

    std::vector<Schema::Path> paths_;
};

} // namespace Rest

} // namespace Net
