/* http_header.h
   Mathieu Stefani, 19 August 2015
   
  Declaration of common http headers
*/

#pragma once

#include "optional.h"
#include <string>
#include <type_traits>
#include <memory>
#include <ostream>
#include <vector>
#include <stdexcept>
#include <cassert>
#include <unordered_map>

#define SAFE_HEADER_CAST

namespace Net {

namespace Http {

#ifdef SAFE_HEADER_CAST
namespace detail {

// compile-time FNV-1a hashing algorithm
static constexpr uint64_t basis = 14695981039346656037ULL;
static constexpr uint64_t prime = 1099511628211ULL;

constexpr uint64_t hash_one(char c, const char* remain, unsigned long long value)
{
    return c == 0 ? value : hash_one(remain[0], remain + 1, (value ^ c) * prime);
}

constexpr uint64_t hash(const char* str)
{
    return hash_one(str[0], str + 1, basis);
}

} // namespace detail
#endif

#ifdef SAFE_HEADER_CAST
    #define NAME(header_name) \
        static constexpr uint64_t Hash = detail::hash(header_name); \
        uint64_t hash() const { return Hash; } \
        static constexpr const char *Name = header_name; \
        const char *name() const { return Name; }
#else
    #define NAME(header_name) \
        static constexpr const char *Name = header_name; \
        const char *name() const { return Name; }
#endif

// 3.5 Content Codings
enum class Encoding {
    Gzip,
    Compress,
    Deflate,
    Identity,
    Unknown
};

const char* encodingString(Encoding encoding);

namespace Mime {

#define MIME_TYPES \
    TYPE(Star       , "*")           \
    TYPE(Text       , "text")        \
    TYPE(Image      , "image")       \
    TYPE(Audio      , "audio")       \
    TYPE(Video      , "video")       \
    TYPE(Application, "application") \
    TYPE(Message    , "message")     \
    TYPE(Multipart  , "multipart")

#define MIME_SUBTYPES \
    SUB_TYPE(Star      , "*")          \
    SUB_TYPE(Plain     , "plain")      \
    SUB_TYPE(Html      , "html")       \
    SUB_TYPE(Xhtml     , "xhtml")      \
    SUB_TYPE(Xml       , "xml")        \
    SUB_TYPE(Javascript, "javascript") \
    SUB_TYPE(Css       , "css")        \
    \
    SUB_TYPE(Json          , "json")                  \
    SUB_TYPE(FormUrlEncoded, "x-www-form-urlencoded") \
    \
    SUB_TYPE(Png, "png") \
    SUB_TYPE(Gif, "gif") \
    SUB_TYPE(Bmp, "bmp") \
    SUB_TYPE(Jpeg, "jpeg")

#define MIME_SUFFIXES \
    SUFFIX(Json       , "json"       , "JavaScript Object Notation")   \
    SUFFIX(Ber        , "ber"        , "Basic Encoding Rules")         \
    SUFFIX(Der        , "der"        , "Distinguished Encoding Rules") \
    SUFFIX(Fastinfoset, "fastinfoset", "Fast Infoset")                 \
    SUFFIX(Wbxml      , "wbxml"      , "WAP Binary XML")               \
    SUFFIX(Zip        , "zip"        , "ZIP file storage")             \
    SUFFIX(Xml        , "xml"        , "Extensible Markup Language")


enum class Type {
#define TYPE(val, _) val,
    MIME_TYPES
#undef TYPE
    None
};

enum class Subtype {
#define SUB_TYPE(val, _) val,
    MIME_SUBTYPES
#undef SUB_TYPE
    Vendor,
    Ext,
    None
};

enum class Suffix {
#define SUFFIX(val, _, __) val,
    MIME_SUFFIXES
#undef SUFFIX
    None,
    Ext
};

// 3.9 Quality Values
class Q {
public:

    // typedef uint8_t Type;

    typedef uint16_t Type;

    explicit Q(Type val)
    {
        if (val > 100) {
            throw std::runtime_error("Invalid quality value, must be in the [0; 100] range");
        }

        val_ = val;
    }

    static Q fromFloat(double f) {
        return Q(static_cast<Type>(f * 100.0));
    }

    Type value() const { return val_; }
    operator Type() const { return val_; }

    std::string toString() const;

private:
    Type val_;
};

inline bool operator==(Q lhs, Q rhs) {
    return lhs.value() == rhs.value();
}

// 3.7 Media Types
class MediaType {
public:
    enum Parse { DoParse, DontParse };

    MediaType()
        : top_(Type::None)
        , sub_(Subtype::None)
        , suffix_(Suffix::None)
    { }

    MediaType(std::string raw, Parse parse = DontParse)
        : top_(Type::None)
        , sub_(Subtype::None)
        , suffix_(Suffix::None)
    {
        if (parse == DoParse) {
            parseRaw(raw.c_str(), raw.length());
        }
        else {
            raw_ = std::move(raw);
        }
    }

    MediaType(Mime::Type top, Mime::Subtype sub)
        : top_(top)
        , sub_(sub)
        , suffix_(Suffix::None)
    { }

    MediaType(Mime::Type top, Mime::Subtype sub, Mime::Suffix suffix)
        : top_(top)
        , sub_(sub)
        , suffix_(suffix)
    { }


    static MediaType fromRaw(const char* str, size_t len);

    static MediaType fromString(const std::string& str);
    static MediaType fromString(std::string&& str);

    Mime::Type top() const { return top_; }
    Mime::Subtype sub() const { return sub_; }
    Mime::Suffix suffix() const { return suffix_; }

    std::string rawSub() const {
        return rawSubIndex.splice(raw_);
    }

    std::string raw() const { return raw_; }

    const Optional<Q>& q() const { return q_; }
    void setQuality(Q quality);

    Optional<std::string> getParam(std::string name) const;
    void setParam(std::string name, std::string value);

    std::string toString() const;
private:
    void parseRaw(const char* str, size_t len);

    Mime::Type top_;
    Mime::Subtype sub_;
    Mime::Suffix suffix_;

    /* Let's save some extra memory allocations by only storing the
       raw MediaType along with indexes of the relevant parts
       Note: experimental for now as it might not be a good idea
    */
    std::string raw_;

    struct Index {
        size_t beg;
        size_t end;

        std::string splice(const std::string& str) const {
            assert(end >= beg);
            return str.substr(beg, end - beg + 1);
        }
    };

    Index rawSubIndex;
    Index rawSuffixIndex;

    std::unordered_map<std::string, std::string> params;

    Optional<Q> q_;
};

inline bool operator==(const MediaType& lhs, const MediaType& rhs) {
    return lhs.top() == rhs.top() &&
           lhs.sub() == rhs.sub() &&
           lhs.suffix() == rhs.suffix();
}

} // namespace Mime

#define MIME(top, sub) \
    Net::Http::Mime::MediaType(Net::Http::Mime::Type::top, Net::Http::Mime::Subtype::sub)

#define MIME3(top, sub, suffix) \
    Net::Http::Mime::MediaType(Net::Http::Mime::Type::top, Net::Http::Mime::Subtype::sub, Net::Http::Mime::Suffix::suffix);


class Header {
public:
    virtual const char *name() const = 0;

    virtual void parse(const std::string& data);
    virtual void parseRaw(const char* str, size_t len);

    virtual void write(std::ostream& stream) const = 0;

#ifdef SAFE_HEADER_CAST
    virtual uint64_t hash() const = 0;
#endif

};

template<typename H> struct IsHeader {

    template<typename T>
    static std::true_type test(decltype(T::Name) *);

    template<typename T>
    static std::false_type test(...);

    static constexpr bool value
        = std::is_base_of<Header, H>::value
       && std::is_same<decltype(test<H>(nullptr)), std::true_type>::value;
};

#ifdef SAFE_HEADER_CAST
template<typename To>
typename std::enable_if<IsHeader<To>::value, std::shared_ptr<To>>::type
header_cast(const std::shared_ptr<Header>& from)
{
    return static_cast<To *>(0)->Hash == from->hash() ?
                std::static_pointer_cast<To>(from) : nullptr;
}

template<typename To>
typename std::enable_if<IsHeader<To>::value, std::shared_ptr<const To>>::type
header_cast(const std::shared_ptr<const Header>& from)
{
    return static_cast<To *>(0)->Hash == from->hash() ?
                std::static_pointer_cast<const To>(from) : nullptr;
}
#endif

class ContentLength : public Header {
public:
    NAME("Content-Length");

    ContentLength()
        : value_(0)
    { }

    explicit ContentLength(uint64_t val)
        : value_(val)
    { }

    void parse(const std::string& data);
    void write(std::ostream& os) const;

    uint64_t value() const { return value_; }

private:
    uint64_t value_;
};

class Host : public Header {
public:
    NAME("Host");

    Host()
     : host_()
     , port_(-1)
    { }

    explicit Host(const std::string& host, int16_t port = -1)
        : host_(host)
        , port_(port)
    { }

    void parse(const std::string& data);
    void write(std::ostream& os) const;

    std::string host() const { return host_; }
    int16_t port() const { return port_; }

private:
    std::string host_;
    int16_t port_;
};

class UserAgent : public Header {
public:
    NAME("User-Agent")

    UserAgent() { }
    explicit UserAgent(const std::string& ua) :
        ua_(ua)
    { }

    void parse(const std::string& data);
    void write(std::ostream& os) const;

    std::string ua() const { return ua_; }

private:
    std::string ua_;
};

class Accept : public Header {
public:
    NAME("Accept")

    Accept() { }

    void parseRaw(const char *str, size_t len);
    void write(std::ostream& os) const;

private:
    std::string data;
};

class ContentEncoding : public Header {
public:
    NAME("Content-Encoding")

    ContentEncoding()
       : encoding_(Encoding::Identity)
    { }

    explicit ContentEncoding(Encoding encoding)
       : encoding_(encoding)
    { }

    void parseRaw(const char* str, size_t len);
    void write(std::ostream& os) const;

    Encoding encoding() const { return encoding_; }

private:
    Encoding encoding_;
};

class Server : public Header {
public:
    NAME("Server")

    Server() { }

    explicit Server(const std::vector<std::string>& tokens);
    explicit Server(const std::string& token);
    explicit Server(const char* token);

    void parse(const std::string& data);
    void write(std::ostream& os) const;

    std::vector<std::string> tokens() const { return tokens_; }
private:
    std::vector<std::string> tokens_;
};

class ContentType : public Header {
public:
    NAME("Content-Type")

    ContentType() { }

    explicit ContentType(const Mime::MediaType& mime) :
        mime_(mime)
    { }

    void parseRaw(const char* str, size_t len);
    void write(std::ostream& os) const;

    Mime::MediaType mime() const { return mime_; }

private:
    Mime::MediaType mime_;

};

} // namespace Http

} // namespace Net

