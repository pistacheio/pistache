/* description.cc
   Mathieu Stefani, 24 février 2016
   
   Implementation of the description system
*/

#include "description.h"
#include "http_header.h"
#include <sstream>
#include <algorithm>
#include "rapidjson/prettywriter.h"

namespace Net {

namespace Rest {

namespace Schema {

Contact::Contact(
        std::string name, std::string url, std::string email)
    : name(std::move(name))
    , url(std::move(url))
    , email(std::move(email))
{ }

License::License(
        std::string name, std::string url)
    : name(std::move(name))
    , url(std::move(url))
{ }

Info::Info(
        std::string title, std::string version, std::string description)
    : title(std::move(title))
    , version(std::move(version))
    , description(std::move(description))
{ }   

PathFragment::PathFragment(
        std::string value, Http::Method method)
    : value(std::move(value))
    , method(method)
{ }

Path::Path(
        std::string value, Http::Method method, std::string description)
    : value(std::move(value))
    , method(method)
    , description(std::move(description))
{ }

bool
PathGroup::hasPath(const std::string& name, Http::Method method) const {
    auto group = paths(name);
    auto it = std::find_if(std::begin(group), std::end(group), [&](const Path& p) {
        return p.method == method;
    });
    return it != std::end(group);
}

bool
PathGroup::hasPath(const Path& path) const {
    return hasPath(path.value, path.method);
}

std::vector<Path>
PathGroup::paths(const std::string& name) const {
    auto it = groups.find(name);
    if (it == std::end(groups))
        return std::vector<Path> { };

    return it->second;
}

Optional<Path>
PathGroup::path(const std::string& name, Http::Method method) const {
    auto group = paths(name);
    auto it = std::find_if(std::begin(group), std::end(group), [&](const Path& p) {
        return p.method == method;
    });

    if (it != std::end(group)) {
        return Some(*it);
    }
    return None();
}

PathGroup::group_iterator
PathGroup::add(Path path) {
    if (hasPath(path))
        return PathGroup::group_iterator { };

    auto &group = groups[path.value];
    return group.insert(group.end(), std::move(path));
}

PathGroup::const_iterator
PathGroup::begin() const {
    return groups.begin();
}

PathGroup::const_iterator
PathGroup::end() const {
    return groups.end();
}

PathGroup::flat_iterator
PathGroup::flatBegin() const {
    return makeFlatMapIterator(groups, begin());
}

PathGroup::flat_iterator
PathGroup::flatEnd()  const {
    return makeFlatMapIterator(groups, end());
}

PathBuilder::PathBuilder(Path* path)
    : path_(path)
{ }

SubPath::SubPath(
        std::string prefix, PathGroup* paths)
    : prefix(std::move(prefix))
   , paths(paths)
{ }

PathBuilder
SubPath::route(std::string name, Http::Method method, std::string description) {
    auto fullPath = prefix + name;
    Path path(std::move(fullPath), method, std::move(description));
    std::copy(std::begin(parameters), std::end(parameters), std::back_inserter(path.parameters));

    auto it = paths->add(std::move(path));

    return PathBuilder(&*it);
}

PathBuilder
SubPath::route(PathFragment fragment, std::string description) {
    return route(std::move(fragment.value), fragment.method, std::move(description));
}

SubPath
SubPath::path(std::string prefix) {
    return SubPath(this->prefix + prefix, paths);
}

Parameter::Parameter(
        std::string name, std::string description)
    : name(std::move(name))
    , description(std::move(description))
    , required(true)
{ } 

Response::Response(
        Http::Code statusCode, std::string description)
    : statusCode(statusCode)
    , description(std::move(description))
{ }

ResponseBuilder::ResponseBuilder(
        Http::Code statusCode, std::string description)
    : response_(statusCode, std::move(description))
{ }

InfoBuilder::InfoBuilder(Info* info)
    : info_(info)
{ }

InfoBuilder&
InfoBuilder::termsOfService(std::string value) {
    info_->termsOfService = std::move(value);
    return *this;
}

InfoBuilder&
InfoBuilder::contact(std::string name, std::string url, std::string email) {
    info_->contact = Some(Contact(std::move(name), std::move(url), std::move(email)));
    return *this;
}

InfoBuilder&
InfoBuilder::license(std::string name, std::string url) {
    info_->license = Some(License(std::move(name), std::move(url)));
    return *this;
}

} // namespace Schema

Description::Description(
        std::string title, std::string version, std::string description)
    : info_(std::move(title), std::move(version), std::move(description))
{
}

Schema::InfoBuilder
Description::info() {
    Schema::InfoBuilder builder(&info_);
    return builder;
}

Description&
Description::host(std::string value) {
    host_ = std::move(value);
    return *this;
}

Schema::PathFragment
Description::get(std::string name) {
    return Schema::PathFragment(std::move(name), Http::Method::Get);
}

Schema::PathFragment
Description::post(std::string name) {
    return Schema::PathFragment(std::move(name), Http::Method::Post);
}

Schema::PathFragment
Description::put(std::string name) {
    return Schema::PathFragment(std::move(name), Http::Method::Put);
}

Schema::PathFragment
Description::del(std::string name) {
    return Schema::PathFragment(std::move(name), Http::Method::Delete);
}

Schema::SubPath
Description::path(std::string name) {
    return Schema::SubPath(std::move(name), &paths_);
}

Schema::PathBuilder
Description::route(std::string name, Http::Method method, std::string description) {
    auto it = paths_.emplace(std::move(name), method, std::move(description));
    return Schema::PathBuilder(&*it);
}

Schema::PathBuilder
Description::route(Schema::PathFragment fragment, std::string description) {
    return route(std::move(fragment.value), fragment.method, std::move(description));
}

Schema::ResponseBuilder
Description::response(Http::Code statusCode, std::string description) {
    Schema::ResponseBuilder builder(statusCode, std::move(description));
    return builder;
}

Swagger&
Swagger::uiPath(std::string path) {
    uiPath_ = std::move(path);
    return *this;
}

Swagger&
Swagger::uiDirectory(std::string dir) {
    uiDirectory_ = std::move(dir);
    return *this;
}

Swagger&
Swagger::apiPath(std::string path) {
    apiPath_ = std::move(path);
    return *this;
}

void
Swagger::install(Rest::Router& router) {

    Route::Handler uiHandler = [=](const Rest::Request& req, Http::ResponseWriter response) {
        auto res = req.resource();

        /*
         * @Export might be useful for routing also. Make it public
         */

        struct Path {
            Path(const std::string& value)
                : value(value)
                , trailingSlashValue(value)
            {
                if (trailingSlashValue.back() != '/')
                    trailingSlashValue += '/';
            }

            static bool hasTrailingSlash(const Rest::Request& req) {
                auto res = req.resource();
                return res.back() == '/';
            }

            std::string stripPrefix(const Rest::Request& req) {
                auto res = req.resource();
                if (!res.compare(0, value.size(), value)) {
                    return res.substr(value.size());
                }
                return res;
            }

            bool matches(const Rest::Request& req) {
                auto res = req.resource();
                if (value == res)
                    return true;

                if (res == trailingSlashValue)
                    return true;

                return false;
            }

            bool isPrefix(const Rest::Request& req) {
                auto res = req.resource();
                return !res.compare(0, value.size(), value);
            }

            const std::string& withTrailingSlash() const {
                return trailingSlashValue;
            }

            std::string join(const std::string& value) const {
                std::string val;
                if (value[0] == '/') val = value.substr(1);
                else val = value;
                return trailingSlashValue + val;
            }

        private:
            std::string value;
            std::string trailingSlashValue;
        };

        Path ui(uiPath_);
        Path uiDir(uiDirectory_);

        if (ui.matches(req)) {
            if (!Path::hasTrailingSlash(req)) {
                response
                    .headers()
                    .add<Http::Header::Location>(uiPath_ + '/');

                response.send(Http::Code::Moved_Permanently);
            } else {
                auto index = uiDir.join("index.html");
                Http::serveFile(response, index.c_str());
            }
            return Route::Result::Ok;
        }
        else if (ui.isPrefix(req)) {
            auto file = ui.stripPrefix(req);
            auto path = uiDir.join(file);
            Http::serveFile(response, path.c_str());
            return Route::Result::Ok;
        }

        else if (res == apiPath_) {
            rapidjson::StringBuffer sb;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);

            description_.serialize(writer);

            response.send(Http::Code::Ok, sb.GetString(), MIME(Application, Json));
            return Route::Result::Ok;
        }

        return Route::Result::Failure;
    };

    router.addCustomHandler(uiHandler);
}


} // namespace Rest

} // namespace Net
