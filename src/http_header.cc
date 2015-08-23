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

    // Parse type
    const char *p = strchr(str, '/');
    if (p == NULL) return res;

    const ptrdiff_t size = p - str;

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
        if (strncmp(str, s, size) == 0) { \
            top = Type::val; \
            break;  \
        }
        MIME_TYPES
#undef TYPE
        else {
            top = Type::Ext;
        }
    } while (0);

    if (top == Type::Ext) return res;

    res.top = top;

    // Parse subtype
    Mime::Subtype sub;

    ++p;

    do {
#define SUB_TYPE(val, s) \
        if (strncmp(p, s, sizeof (s) - 1) == 0) { \
            sub = Subtype::val; \
            p += sizeof(s) - 1; \
            break; \
        }
        MIME_SUBTYPES
#undef SUB_TYPE
        else {
            sub = Subtype::Ext;
        }
    } while (0);

    if (sub == Subtype::Ext) return res;
    res.sub = sub;

    // Parse suffix
    Mime::Suffix suffix = Suffix::None;
    if (*p == '+') {
        ++p;

        do {
#define SUFFIX(val, s, _) \
            if (strncmp(p, s, sizeof (s) - 1) == 0) { \
                suffix = Suffix::val; \
                break; \
            }
            MIME_SUFFIXES
#undef SUFFIX
            else {
                suffix = Suffix::Ext;
            }
        } while (0);

        res.suffix = suffix;
    }

    return res;

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
    res += topString(top);
    res += "/";
    res += subString(sub);
    if (suffix != Suffix::None) {
        res += suffixString(suffix);
    }

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
