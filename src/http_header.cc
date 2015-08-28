/* http_header.cc
   Mathieu Stefani, 19 August 2015
   
   Implementation of common HTTP headers described by the RFC
*/

#include "http_header.h"
#include "common.h"
#include <stdexcept>
#include <iterator>
#include <cstring>
#include <iostream>

using namespace std;

namespace Net {

namespace Http {

namespace Mime {

std::string
Q::toString() const {
    if (val_ == 0)
        return "q=0";
    else if (val_ == 100)
        return "q=1";

    char buff[sizeof("q=0.99")];
    memset(buff, sizeof buff, 0);
    if (val_ % 10 == 0)
        snprintf(buff, sizeof buff, "q=%.1f", val_ / 100.0);
    else
        snprintf(buff, sizeof buff, "q=%.2f", val_ / 100.0);

    return std::string(buff);
}

MediaType
MediaType::fromString(const std::string& str) {
    return fromRaw(str.c_str(), str.size());
}

MediaType
MediaType::fromString(std::string&& str) {
    return fromRaw(str.c_str(), str.size());
}

MediaType
MediaType::fromRaw(const char* str, size_t len) {
    MediaType res;

    // HUGE @Todo: Validate the input when parsing to avoid overflow

    auto eof = [&](const char *p) {
        return p - str == len;
    };

    #define MAX_SIZE(s) std::min(sizeof(s) - 1, static_cast<size_t>(p - str))

    // Parse type
    const char *p = strchr(str, '/');
    if (p == NULL) return res;

    Mime::Type top;

    // The reason we are using a do { } while (0); syntax construct here is to emulate
    // if / else-if. Since we are using items-list macros to compare the strings,
    // we want to avoid evaluating all the branches when one of them evaluates to true.
    //
    // Instead, we break the loop when a branch evaluates to true so that we do
    // not evaluate all the subsequent ones.
    //
    // Watch out, this pattern is repeated throughout the function
    do {
#define TYPE(val, s) \
        if (memcmp(str, s, MAX_SIZE(s)) == 0) { \
            top = Type::val; \
            break;  \
        }
        MIME_TYPES
#undef TYPE
        top = Type::Ext;
    } while (0);

    res.top_ = top;
    if (top == Type::Ext) return res;

    // Parse subtype
    Mime::Subtype sub;

    ++p;

    if (memcmp(p, "vnd.", 4) == 0) {
        sub = Subtype::Vendor;
        while (!eof(p) && (*p != ';' && *p != '+')) ++p;
    } else {
        do {
#define SUB_TYPE(val, s) \
            if (memcmp(p, s, MAX_SIZE(s)) == 0) { \
                sub = Subtype::val; \
                p += sizeof(s) - 1; \
                break; \
            }
            MIME_SUBTYPES
#undef SUB_TYPE
            sub = Subtype::Ext;
        } while (0);
    }

    res.sub_ = sub;

    // Parse suffix
    Mime::Suffix suffix = Suffix::None;
    if (*p == '+') {
        ++p;

        do {
#define SUFFIX(val, s, _) \
            if (memcmp(p, s, MAX_SIZE(s)) == 0) { \
                suffix = Suffix::val; \
                p += sizeof(s) - 1; \
                break; \
            }
            MIME_SUFFIXES
#undef SUFFIX
            suffix = Suffix::Ext;
        } while (0);

        res.suffix_ = suffix;
    }

    if (eof(p)) return res;

    if (*p == ';') ++p;

    while (*p == ' ') ++p;

    Optional<Q> q = None();

    if (*p == 'q') {
        ++p;

        if (*p == '=') {
            char *end;
            double val = strtod(p + 1, &end);
            q = Some(Q::fromFloat(val));
        }
    }

    res.q_ = std::move(q);

    #undef MAX_SIZE

    return res;

}

void
MediaType::setQuality(Q quality) {
    q_ = Some(quality);
}

std::string
MediaType::toString() const {

    auto topString = [](Mime::Type top) -> const char * {
        switch (top) {
#define TYPE(val, str) \
        case Mime::Type::val: \
            return str;
        MIME_TYPES
#undef TYPE
        }
    };

    auto subString = [](Mime::Subtype sub) -> const char * {
        switch (sub) {
#define SUB_TYPE(val, str) \
        case Mime::Subtype::val: \
            return str;
        MIME_SUBTYPES
#undef TYPE
        }
    };

    auto suffixString = [](Mime::Suffix suffix) -> const char * {
        switch (suffix) {
#define SUFFIX(val, str, _) \
        case Mime::Suffix::val: \
            return "+" str;
        MIME_SUFFIXES
#undef SUFFIX
        }
    };

    std::string res;
    res += topString(top_);
    res += "/";
    res += subString(sub_);
    if (suffix_ != Suffix::None) {
        res += suffixString(suffix_);
    }

    optionally_do(q_, [&res](Q quality) {
        res += "; ";
        res += quality.toString();
    });

    return res;
}

} // namespace Mime

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
        port_ = -1;
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
}

void
ContentType::write(std::ostream& os) const {
    os << "Content-Type: ";
    os << mime_.toString();
}

} // namespace Http

} // namespace Net
