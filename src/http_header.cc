/* http_header.cc
   Mathieu Stefani, 19 August 2015
   
   Implementation of common HTTP headers described by the RFC
*/

#include "http_header.h"
#include "common.h"
#include "http.h"
#include <stdexcept>
#include <iterator>
#include <cstring>
#include <iostream>

using namespace std;

namespace Net {

namespace Http {

namespace Header {

const char* encodingString(Encoding encoding) {
    switch (encoding) {
    case Encoding::Gzip:
        return "gzip";
    case Encoding::Compress:
        return "compress";
    case Encoding::Deflate:
        return "deflate";
    case Encoding::Identity:
        return "identity";
    case Encoding::Unknown:
        return "unknown";
    }

    unreachable();
}

void
Header::parse(const std::string& data) {
    parseRaw(data.c_str(), data.size());
}

void
Header::parseRaw(const char *str, size_t len) {
    parse(std::string(str, len));
}

void
Allow::parseRaw(const char* str, size_t len) {
}

void
Allow::write(std::ostream& os) const {
    /* This puts an extra ',' at the end :/
    std::copy(std::begin(methods_), std::end(methods_),
              std::ostream_iterator<Http::Method>(os, ", "));
    */

    for (std::vector<Http::Method>::size_type i = 0; i < methods_.size(); ++i) {
        os << methods_[i];
        if (i < methods_.size() - 1) os << ", ";
    }
}

void
Allow::addMethod(Http::Method method) {
    methods_.push_back(method);
}

void
Allow::addMethods(std::initializer_list<Method> methods) {
    std::copy(std::begin(methods), std::end(methods), std::back_inserter(methods_));
}

void
Allow::addMethods(const std::vector<Http::Method>& methods)
{
    std::copy(std::begin(methods), std::end(methods), std::back_inserter(methods_));
}

CacheControl::CacheControl(Http::CacheDirective directive)
{
    directives_.push_back(directive);
}

void
CacheControl::parseRaw(const char* str, size_t len) {
    using Http::CacheDirective;

    auto eof = [&](const char *p) {
        return p - str == len;
    };

#define MAX_SIZE(s) \
    std::min(sizeof(s) - 1, len - (begin - str))

#define TRY_PARSE_TRIVIAL_DIRECTIVE(dstr, directive)                      \
    if (memcmp(begin, dstr, MAX_SIZE(dstr)) == 0) {                       \
        directives_.push_back(CacheDirective(CacheDirective::directive)); \
        begin += sizeof(dstr) - 1;                                        \
        break;                                                            \
    }                                                                     \
    (void) 0

    // @Todo: check for overflow
#define TRY_PARSE_TIMED_DIRECTIVE(dstr, directive)                                                    \
    if (memcmp(begin, dstr, MAX_SIZE(dstr)) == 0) {                                                   \
        const char *p = static_cast<const char *>(memchr(str, '=', len));                             \
        if (p == NULL) {                                                                              \
            throw std::runtime_error("Invalid caching directive, missing delta-seconds");             \
        }                                                                                             \
        char *end;                                                                                    \
        int secs = strtol(p + 1, &end, 10);                                                           \
        if (!eof(end) && *end != ',') {                                                               \
            throw std::runtime_error("Invalid caching directive, malformated delta-seconds");         \
        }                                                                                             \
        directives_.push_back(CacheDirective(CacheDirective::directive, std::chrono::seconds(secs))); \
        begin = end;                                                                                  \
        break;                                                                                        \
    }                                                                                                 \
    (void) 0

    const char *begin = str;
    do {

        do {
            TRY_PARSE_TRIVIAL_DIRECTIVE("no-cache", NoCache);
            TRY_PARSE_TRIVIAL_DIRECTIVE("no-store", NoStore);
            TRY_PARSE_TRIVIAL_DIRECTIVE("no-transform", NoTransform);
            TRY_PARSE_TRIVIAL_DIRECTIVE("only-if-cached", OnlyIfCached);
            TRY_PARSE_TRIVIAL_DIRECTIVE("public", Public);
            TRY_PARSE_TRIVIAL_DIRECTIVE("private", Private);
            TRY_PARSE_TRIVIAL_DIRECTIVE("must-revalidate", MustRevalidate);
            TRY_PARSE_TRIVIAL_DIRECTIVE("proxy-revalidate", ProxyRevalidate);

            TRY_PARSE_TIMED_DIRECTIVE("max-age", MaxAge);
            TRY_PARSE_TIMED_DIRECTIVE("max-stale", MaxStale);
            TRY_PARSE_TIMED_DIRECTIVE("min-fresh", MinFresh);
            TRY_PARSE_TIMED_DIRECTIVE("s-maxage", SMaxAge);

        } while (false);

        if (!eof(begin)) {
            if (*begin != ',')
                throw std::runtime_error("Invalid caching directive, expected a comma");

            while (!eof(begin) && *begin == ',' || *begin == ' ') ++begin;
        }

    } while (!eof(begin));

#undef TRY_PARSE_TRIVIAL_DIRECTIVE
#undef TRY_PARSE_TIMED_DIRECTIVE

}

void
CacheControl::write(std::ostream& os) const {
    using Http::CacheDirective;

    auto directiveString = [](CacheDirective directive) -> const char* const {
        switch (directive.directive()) {
            case CacheDirective::NoCache:
                return "no-cache";
            case CacheDirective::NoStore:
                return "no-store";
            case CacheDirective::NoTransform:
                return "no-transform";
            case CacheDirective::OnlyIfCached:
                return "only-if-cached";
            case CacheDirective::Public:
                return "public";
            case CacheDirective::Private:
                return "private";
            case CacheDirective::MustRevalidate:
                return "must-revalidate";
            case CacheDirective::ProxyRevalidate:
                return "proxy-revalidate";
            case CacheDirective::MaxAge:
                return "max-age";
            case CacheDirective::MaxStale:
                return "max-stale";
            case CacheDirective::MinFresh:
                return "min-fresh";
            case CacheDirective::SMaxAge:
                return "s-maxage";
        }
    };

    auto hasDelta = [](CacheDirective directive) {
        switch (directive.directive()) {
            case CacheDirective::MaxAge:
            case CacheDirective::MaxStale:
            case CacheDirective::MinFresh:
            case CacheDirective::SMaxAge:
                return true;
        }
        return false;
    };

    for (std::vector<CacheDirective>::size_type i = 0; i < directives_.size(); ++i) {
        const auto& d = directives_[i];
        os << directiveString(d);
        if (hasDelta(d)) {
            auto delta = d.delta();
            if (delta.count() > 0) {
                os << "=" << delta.count();
            }
        }

        if (i < directives_.size() - 1) {
            os << ", ";
        }
    }


}

void
CacheControl::addDirective(Http::CacheDirective directive) {
    directives_.push_back(directive);
}

void
CacheControl::addDirectives(const std::vector<Http::CacheDirective>& directives) {
    std::copy(std::begin(directives), std::end(directives), std::back_inserter(directives_));
}

void
ContentLength::parse(const std::string& data) {
    try {
        size_t pos;
        uint64_t val = std::stoi(data, &pos);
        if (pos != 0) {
        }

        value_ = val;
    } catch (const std::invalid_argument& e) {
    }
}

void
ContentLength::write(std::ostream& os) const {
    os << "Content-Length: " << value_;
}

void
Host::parse(const std::string& data) {
    auto pos = data.find(':');
    if (pos != std::string::npos) {
        std::string h = data.substr(0, pos);
        int16_t p = std::stoi(data.substr(pos + 1));

        host_ = h;
        port_ = p;
    } else {
        host_ = data;
        port_ = 80;
    }
}

void
Host::write(std::ostream& os) const {
    os << host_;
    if (port_ != -1) {
        os << ":" << port_;
    }
}

void
UserAgent::parse(const std::string& data) {
    ua_ = data;
}

void
UserAgent::write(std::ostream& os) const {
    os << "User-Agent: " << ua_;
}

void
Accept::parseRaw(const char *str, size_t len) {

    auto remaining = [&](const char* p) {
        return len - (p - str);
    };

    auto eof = [&](const char *p) {
        return remaining(p) == 0;
    };

    const char *p = static_cast<const char *>(memchr(str, ',', len));
    const char *begin = str;
    if (p == NULL) {
        mediaRange_.push_back(Mime::MediaType::fromRaw(str, len));
    } else {
        do {

            const size_t mimeLen = p - begin;
            mediaRange_.push_back(Mime::MediaType::fromRaw(begin, mimeLen));

            while (!eof(p) && (*p == ',' || *p == ' ')) ++p;
            if (eof(p)) throw std::runtime_error("Invalid format for Accept header");
            begin = p;

            p = static_cast<const char *>(memchr(p, ',', remaining(p)));

        } while (p != NULL);

        mediaRange_.push_back(Mime::MediaType::fromRaw(begin, remaining(begin)));
    }

}

void
Accept::write(std::ostream& os) const {
}

void
ContentEncoding::parseRaw(const char* str, size_t len) {
    // TODO: case-insensitive
    //
    if (!strncmp(str, "gzip", len)) {
        encoding_ = Encoding::Gzip;
    }
    else if (!strncmp(str, "deflate", len)) {
        encoding_ = Encoding::Deflate;
    }
    else if (!strncmp(str, "compress", len)) {
        encoding_ = Encoding::Compress;
    }
    else if (!strncmp(str, "identity", len)) {
        encoding_ = Encoding::Identity;
    }
    else {
        encoding_ = Encoding::Unknown;
    }
}

void
ContentEncoding::write(std::ostream& os) const {
    os << "Content-Encoding: " << encodingString(encoding_);
}

Server::Server(const std::vector<std::string>& tokens)
    : tokens_(tokens)
{ }

Server::Server(const std::string& token)
{
    tokens_.push_back(token);
}

Server::Server(const char* token)
{
    tokens_.emplace_back(token);
}

void
Server::parse(const std::string& data)
{
}

void
Server::write(std::ostream& os) const
{
    os << "Server: ";
    std::copy(std::begin(tokens_), std::end(tokens_),
                 std::ostream_iterator<std::string>(os, " "));
}

void
ContentType::parseRaw(const char* str, size_t len)
{
    mime_.parseRaw(str, len);
}

void
ContentType::write(std::ostream& os) const {
    os << "Content-Type: ";
    os << mime_.toString();
}

} // namespace Header

} // namespace Http

} // namespace Net
