/* mime.cc
   Mathieu Stefani, 29 August 2015
   
   Implementaton of MIME Type parsing
*/

#include "mime.h"
#include "http.h"
#include <cstring>

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

    res.parseRaw(str, len);
    return res;
}

void
MediaType::parseRaw(const char* str, size_t len) {
    auto eof = [&](const char *p) {
        return p - str == len;
    };

    auto offset = [&](const char* ptr) {
        return static_cast<size_t>(ptr - str);
    };

    auto raise = [&](const char* str) {
        // TODO: eventually, we should throw a more generic exception
        // that could then be catched in lower stack frames to rethrow
        // an HttpError
        throw HttpError(Http::Code::Unsupported_Media_Type, str);
    };

    // Macro to ensure that we do not overflow when comparing strings
    // The trick here is to use sizeof on a raw string literal of type
    // const char[N] instead of strlen to avoid additional
    // runtime computation
    #define MAX_SIZE(s) std::min(sizeof(s) - 1, len - offset(p))

    // Parse type
    const char *p = strchr(str, '/');
    if (p == NULL) {
        raise("Malformated Media Type");
    }

    raw_ = string(str, len);

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
#define TYPE(val, s)                            \
        if (memcmp(str, s, MAX_SIZE(s)) == 0) { \
            top = Type::val;                    \
            break;                              \
        }
        MIME_TYPES
#undef TYPE
        raise("Unknown Media Type");
    } while (0);

    top_ = top;

    // Parse subtype
    Mime::Subtype sub;
    ++p;

    if (eof(p)) raise("Malformed Media Type");

    if (memcmp(p, "vnd.", MAX_SIZE("vnd.")) == 0) {
        sub = Subtype::Vendor;
    } else {
        do {
#define SUB_TYPE(val, s)                          \
            if (memcmp(p, s, MAX_SIZE(s)) == 0) { \
                sub = Subtype::val;               \
                p += sizeof(s) - 1;               \
                break;                            \
            }
            MIME_SUBTYPES
#undef SUB_TYPE
            sub = Subtype::Ext;
        } while (0);
    }

    if (sub == Subtype::Ext || sub == Subtype::Vendor) {
        rawSubIndex.beg = offset(p);
        while (!eof(p) && (*p != ';' && *p != '+')) ++p;
        rawSubIndex.end = offset(p) - 1;
    }

    sub_ = sub;

    if (eof(p)) return;

    // Parse suffix
    Mime::Suffix suffix = Suffix::None;
    if (*p == '+') {

        ++p;

        if (eof(p)) raise("Malformed Media Type");

        do {
#define SUFFIX(val, s, _)                         \
            if (memcmp(p, s, MAX_SIZE(s)) == 0) { \
                suffix = Suffix::val;             \
                p += sizeof(s) - 1;               \
                break;                            \
            }
            MIME_SUFFIXES
#undef SUFFIX
            suffix = Suffix::Ext;
        } while (0);

        if (suffix == Suffix::Ext) {
            rawSuffixIndex.beg = offset(p);
            while (!eof(p) && (*p != ';' && *p != '+')) ++p;
            rawSuffixIndex.end = offset(p) - 1;
        }

        suffix_ = suffix;
    }

    // Parse parameters
    while (!eof(p)) {

        if (*p == ';' || *p == ' ') {
            if (eof(p + 1)) raise("Malformed Media Type");
            ++p;
        }

        else if (*p == 'q') {
            ++p;

            if (eof(p)) {
                raise("Invalid quality factor");
            }

            if (*p == '=') {
                char *end;
                double val = strtod(p + 1, &end);
                if (!eof(end) && *end != ';' && *end != ' ') {
                    raise("Invalid quality factor");
                }
                q_ = Some(Q::fromFloat(val));
                p = end;
            }
            else {
                raise("Invalid quality factor");
            }
        }
        else {
            const char *begin = p;
            while (!eof(p) && *p != '=') ++p;

            if (eof(p)) raise("Unfinished Media Type parameter");
            const char *end = p;
            ++p;
            if (eof(p)) raise("Unfinished Media Type parameter");

            std::string key(begin, end);

            begin = p;
            while (!eof(p) && *p != ' ' && *p != ';') ++p;

            std::string value(begin, p);

            params.insert(std::make_pair(std::move(key), std::move(value)));

        }

    }

    #undef MAX_SIZE

}

void
MediaType::setQuality(Q quality) {
    q_ = Some(quality);
}

Optional<std::string>
MediaType::getParam(std::string name) const {
    auto it = params.find(name);
    if (it == std::end(params)) {
        return None();
    }

    return Some(it->second);
}

void
MediaType::setParam(std::string name, std::string value) {
    params[name] = value;
}

std::string
MediaType::toString() const {

    if (!raw_.empty()) return raw_;

    auto topString = [](Mime::Type top) -> const char * {
        switch (top) {
#define TYPE(val, str)        \
        case Mime::Type::val: \
            return str;
        MIME_TYPES
#undef TYPE
        }
    };

    auto subString = [](Mime::Subtype sub) -> const char * {
        switch (sub) {
#define SUB_TYPE(val, str)       \
        case Mime::Subtype::val: \
            return str;
        MIME_SUBTYPES
#undef TYPE
        }
    };

    auto suffixString = [](Mime::Suffix suffix) -> const char * {
        switch (suffix) {
#define SUFFIX(val, str, _)     \
        case Mime::Suffix::val: \
            return "+" str;
        MIME_SUFFIXES
#undef SUFFIX
        }
    };

    // @Improvement: allocating and concatenating many small strings is probably slow
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

    for (const auto& param: params) {
        res += "; ";
        res += param.first + "=" + param.second;
    }

    return res;
}

} // namespace Mime

} // namespace Http

} // namespace Net

