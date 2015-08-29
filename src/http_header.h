/* http_header.h
   Mathieu Stefani, 19 August 2015
   
  Declaration of common http headers
*/

#pragma once

#include "mime.h"
#include <string>
#include <type_traits>
#include <memory>
#include <ostream>
#include <vector>

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

