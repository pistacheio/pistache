/* description.cc
   Mathieu Stefani, 24 février 2016
   
   Implementation of the description system
*/

#include "description.h"
#include <sstream>

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
        std::string title, std::string description, std::string version)
    : title(std::move(title))
    , description(std::move(description))
    , version(std::move(version))
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

PathBuilder::PathBuilder(Path* path)
    : path_(path)
{ }

SubPath::SubPath(
        std::string prefix, std::vector<Path>* paths)
    : prefix(std::move(prefix))
   , paths(paths)
{ }

PathBuilder
SubPath::route(std::string name, Http::Method method, std::string description) {
    auto fullPath = prefix + name;
    Path path(std::move(fullPath), method, std::move(description));
    std::copy(std::begin(parameters), std::end(parameters), std::back_inserter(path.parameters));

    paths->push_back(std::move(path));

    return PathBuilder(&paths->back());
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
    paths_.emplace_back(std::move(name), method, std::move(description));
    return Schema::PathBuilder(&paths_.back());
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


} // namespace Rest

} // namespace Net
