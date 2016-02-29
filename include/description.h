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
#include <algorithm>
#include "http_defs.h"
#include "mime.h"
#include "optional.h"
#include "router.h"
#include "iterator_adapter.h"

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

#define SCHEMES            \
    SCHEME(Http , "http")  \
    SCHEME(Https, "https") \
    SCHEME(Ws   , "ws")    \
    SCHEME(Wss  , "wss")   \



enum class Scheme {
#define SCHEME(e, _) e,
    SCHEMES
#undef SCHEME
};

const char* schemeString(Scheme scheme);

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

    template<typename Writer>
    void serialize(Writer& writer) const {
        writer.String("swagger");
        writer.String("2.0");
        writer.String("info");
        writer.StartObject();
        {
            writer.String("title");
            writer.String(title.c_str());
            writer.String("version");
            writer.String(version.c_str());
            if (!description.empty()) {
                writer.String("description");
                writer.String(description.c_str());
            }
            if (!termsOfService.empty()) {
                writer.String("termsOfService");
                writer.String(termsOfService.c_str());
            }
        }
        writer.EndObject();
    }
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

    template<typename Writer>
    void serialize(Writer& writer) const {
        writer.StartObject();
        {
            writer.String("name");
            writer.String(name.c_str());
            writer.String("in");
            // @Feature: support other types of parameters
            writer.String("path");
            writer.String("description");
            writer.String(description.c_str());
            writer.String("required");
            writer.Bool(required);
            writer.String("type");
            writer.String(type->typeName());
        }
        writer.EndObject();
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

    template<typename Writer>
    void serialize(Writer& writer) const {
        auto code = std::to_string(static_cast<uint32_t>(statusCode));
        writer.String(code.c_str());
        writer.StartObject();
        {
            writer.String("description");
            writer.String(description.c_str());
        }
        writer.EndObject();
    }
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
    bool hidden;

    std::vector<Http::Mime::MediaType> produceMimes;
    std::vector<Http::Mime::MediaType> consumeMimes;
    std::vector<Parameter> parameters;
    std::vector<Response> responses;

    Route::Handler handler;

    static std::string swaggerFormat(const std::string& path);

    bool isBound() const {
        return handler != nullptr;
    }

    template<typename Writer>
    void serialize(Writer& writer) const {
        auto serializeMimes = [&](const char* name, const std::vector<Http::Mime::MediaType>& mimes) {
            if (!mimes.empty()) {
                writer.String(name);
                writer.StartArray();
                {
                    for (const auto& mime: mimes) {
                        auto str = mime.toString();
                        writer.String(str.c_str());
                    }
                }
                writer.EndArray();
            }
        };

        std::string methodStr(methodString(method));
        // So it looks like Swagger requires method to be in lowercase
        std::transform(std::begin(methodStr), std::end(methodStr), std::begin(methodStr), ::tolower);

        writer.String(methodStr.c_str());
        writer.StartObject();
        {
            writer.String("description");
            writer.String(description.c_str());
            serializeMimes("consumes", consumeMimes);
            serializeMimes("produces", produceMimes);
            if (!parameters.empty()) {
                writer.String("parameters");
                writer.StartArray();
                {
                    for (const auto& parameter: parameters) {
                        parameter.serialize(writer);
                    }
                }
                writer.EndArray();
            }
            if (!responses.empty()) {
                writer.String("responses");
                writer.StartObject();
                {
                    for (const auto& response: responses) {
                        response.serialize(writer);
                    }
                }
                writer.EndObject();
            }
        }
        writer.EndObject();
    }
};

class PathGroup {
public:
    struct Group : public std::vector<Path> {
        bool isHidden() const {
            if (empty()) return false;

            return std::all_of(begin(), end(), [](const Path& path) {
                return path.hidden;
            });
        }
    };

    typedef std::unordered_map<std::string, Group> Map;
    typedef Map::iterator iterator;
    typedef Map::const_iterator const_iterator;

    typedef std::vector<Path>::iterator group_iterator;

    typedef FlatMapIteratorAdapter<Map> flat_iterator;

    enum class Format { Default, Swagger };

    bool hasPath(const std::string& name, Http::Method method) const;
    bool hasPath(const Path& path) const;

    Group paths(const std::string& name) const;
    Optional<Path> path(const std::string& name, Http::Method method) const;

    group_iterator add(Path path);

    template<typename... Args>
    group_iterator emplace(Args&& ...args) {
        return add(Path(std::forward<Args>(args)...));
    }

    const_iterator begin() const;
    const_iterator end() const;

    flat_iterator flatBegin() const;
    flat_iterator flatEnd() const;

    template<typename Writer>
    void serialize(
            Writer& writer, const std::string& prefix, Format format = Format::Default) const {
        writer.String("paths");
        writer.StartObject();
        {
            for (const auto& group: groups) {
                if (group.second.isHidden()) continue;

                std::string name(group.first);
                if (!prefix.empty()) {
                    if (!name.compare(0, prefix.size(), prefix)) {
                        name = name.substr(prefix.size());
                    }
                }

                if (format == Format::Default) {
                    writer.String(name.c_str());
                } else {
                    auto swaggerPath = Path::swaggerFormat(name);
                    writer.String(swaggerPath.c_str());
                }
                writer.StartObject();
                {
                    for (const auto& path: group.second) {
                        if (!path.hidden)
                            path.serialize(writer);
                    }
                }
                writer.EndObject();
            }
        }
        writer.EndObject();
    }

private:
    Map groups;
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

            return Route::Result::Ok;
        };

        #undef CALL_MEMBER_FN

        return *this;
    }

    template<typename Result, typename... Args>
    PathBuilder& bind(Result (*func)(Args...)) {

        path_->handler = [=](const Rest::Request& request, Http::ResponseWriter response) {
            func(request, std::move(response));

            return Route::Result::Ok;
        };

        return *this;
    }

    PathBuilder&
    hide(bool value = true) {
        path_->hidden = value;
    }

private:
    Path* path_;
};

struct SubPath {
    SubPath(std::string prefix, PathGroup* paths);

    PathBuilder route(std::string path, Http::Method method, std::string description = "");
    PathBuilder route(PathFragment fragment, std::string description = "");

    SubPath path(std::string prefix);

    template<typename T>
    void parameter(std::string name, std::string description) {
        parameters.push_back(Parameter::create<T>(std::move(name), std::move(description)));
    }

    std::string prefix;
    std::vector<Parameter> parameters;
    PathGroup* paths;
};

} // namespace Schema

class Description {
public:
    Description(std::string title, std::string version, std::string description = "");

    Schema::InfoBuilder info();

    Description& host(std::string value);
    Description& basePath(std::string value);
    template<typename... Schemes>
    Description& schemes(Schemes... schemes) {

        const Scheme s[] = { schemes... };
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

    Schema::PathGroup paths() const { return paths_; }

    template<typename Writer>
    void serialize(Writer& writer) const {
        writer.StartObject();
        {
            info_.serialize(writer);
            if (!host_.empty()) {
                writer.String("host");
                writer.String(host_.c_str());
            }
            if (!basePath_.empty()) {
                writer.String("basePath");
                writer.String(basePath_.c_str());
            }
            if (!schemes_.empty()) {
                writer.String("schemes");
                writer.StartArray();
                {
                    for (const auto& scheme: schemes_) {
                        writer.String(schemeString(scheme));
                    }
                }
                writer.EndArray();
            }

            paths_.serialize(writer, basePath_, Schema::PathGroup::Format::Swagger);
        }
        writer.EndObject();
    }

private:
    Schema::Info info_;
    std::string host_;
    std::string basePath_;
    std::vector<Scheme> schemes_;

    Schema::PathGroup paths_;
};

class Swagger {
public:
    Swagger(const Description& description)
        : description_(description)
    { }

    Swagger& uiPath(std::string path);
    Swagger& uiDirectory(std::string dir);
    Swagger& apiPath(std::string path);
   
    void install(Rest::Router& router);

private:
    Description description_;
    std::string uiPath_;
    std::string uiDirectory_;
    std::string apiPath_;
};

} // namespace Rest

} // namespace Net
