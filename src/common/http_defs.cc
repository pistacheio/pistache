/* http_defs.cc
   Mathieu Stefani, 01 September 2015
   
   Implementation of http definitions
*/

#include <iostream>
#include <iomanip>

#include <pistache/http_defs.h>
#include <pistache/common.h>

namespace Pistache {
namespace Http {

namespace {
    bool parseRFC1123Date(std::tm& tm, const char* str, size_t len) {
        char *p = strptime(str, "%a, %d %b %Y %H:%M:%S %Z", &tm);
        return p != NULL;
    }

    bool parseRFC850Date(std::tm& tm, const char* str, size_t len) {
        char *p = strptime(str, "%A, %d-%b-%y %H:%M:%S %Z", &tm);
        return p != NULL;
    }

    bool parseAscTimeDate(std::tm& tm, const char* str, size_t len) {
        char *p = strptime(str, "%a %b  %d %H:%M:%S %Y", &tm);
        return p != NULL;
    }
} // anonymous namespace

CacheDirective::CacheDirective(Directive directive)
{
    init(directive, std::chrono::seconds(0));
}

CacheDirective::CacheDirective(Directive directive, std::chrono::seconds delta)
{
    init(directive, delta);
}

std::chrono::seconds
CacheDirective::delta() const
{
    switch (directive_) {
        case MaxAge:
            return std::chrono::seconds(data.maxAge);
        case SMaxAge:
            return std::chrono::seconds(data.sMaxAge);
        case MaxStale:
            return std::chrono::seconds(data.maxStale);
        case MinFresh:
            return std::chrono::seconds(data.minFresh);
    }

    throw std::domain_error("Invalid operation on cache directive");    
}

void
CacheDirective::init(Directive directive, std::chrono::seconds delta)
{
    directive_ = directive;
    switch (directive) {
        case MaxAge:
            data.maxAge = delta.count();
            break;
        case SMaxAge:
            data.sMaxAge = delta.count();
            break;
        case MaxStale:
            data.maxStale = delta.count();
            break;
        case MinFresh:
            data.minFresh = delta.count();
            break;
    }
}

FullDate::FullDate() {
    std::memset(&date_, 0, sizeof date_);
}

FullDate
FullDate::fromRaw(const char* str, size_t len)
{
    // As per the RFC, implementation MUST support all three formats.
    std::tm tm = {};
    if (parseRFC1123Date(tm, str, len)) {
        return FullDate(tm);
    }

    memset(&tm, 0, sizeof tm);
    if (parseRFC850Date(tm, str, len)) {
        return FullDate(tm);
    }
    memset(&tm, 0, sizeof tm);

    if (parseAscTimeDate(tm, str, len)) {
        return FullDate(tm);
    }

    throw std::runtime_error("Invalid Date format");
}

FullDate
FullDate::fromString(const std::string& str) {
    return FullDate::fromRaw(str.c_str(), str.size());
}

void
FullDate::write(std::ostream& os, Type type) const
{
    char buff[100];
    std::memset(buff, 0, sizeof buff);
    switch (type) {
    case Type::RFC1123:
        //os << std::put_time(&date_, "%a, %d %b %Y %H:%M:%S %Z");
        if (std::strftime(buff, sizeof buff, "%a, %d %b %Y %H:%M:%S %Z", &date_))
            os << buff;
        break;
    case Type::RFC850:
    case Type::AscTime:
        break;
    }
}

const char *versionString(Version version) {
    switch (version) {
        case Version::Http10:
            return "HTTP/1.0";
        case Version::Http11:
            return "HTTP/1.1";
    }

    unreachable();
}

const char* methodString(Method method)
{
    switch (method) {
#define METHOD(name, str) \
    case Method::name: \
        return str;
    HTTP_METHODS
#undef METHOD
    }

    unreachable();
}

const char* codeString(Code code)
{
    switch (code) {
#define CODE(_, name, str) \
    case Code::name: \
         return str;
    STATUS_CODES
#undef CODE
    }

    return "";
}

std::ostream& operator<<(std::ostream& os, Version version) {
    os << versionString(version);
    return os;
}

std::ostream& operator<<(std::ostream& os, Method method) {
    os << methodString(method);
    return os;
}

std::ostream& operator<<(std::ostream& os, Code code) {
    os << codeString(code);
    return os;
}

HttpError::HttpError(Code code, std::string reason)
    : code_(static_cast<int>(code))
    , reason_(std::move(reason))
{ }

HttpError::HttpError(int code, std::string reason)
    : code_(code)
    , reason_(std::move(reason))
{ }


} // namespace Http
} // namespace Pistache
