/* http_defs.cc
   Mathieu Stefani, 01 September 2015
   
   Implementation of http definitions
*/

#include "http_defs.h"
#include "common.h"

namespace Net {

namespace Http {

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

std::ostream& operator<<(std::ostream& os, Method method) {
    os << methodString(method);
    return os;
}

std::ostream& operator<<(std::ostream& os, Code code) {
    os << codeString(code);
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

} // namespace Net
