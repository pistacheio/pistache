/* http_header.h
   Mathieu Stefani, 19 August 2015
   
  Declaration of common http headers
*/

#pragma once

#include <string>

#define NAME(header_name) \
    static constexpr const char *Name = header_name; \
    const char *name() const { return Name; }

namespace Net {

namespace Http {

class Header {
public:
    virtual void parse(const std::string& data) = 0;
    virtual void parseRaw(const char* str, size_t len);

    virtual const char *name() const = 0;

    //virtual void write(Net::Tcp::Stream& writer) = 0;
};

class ContentLength : public Header {
public:
    NAME("Content-Length");

    void parse(const std::string& data);
    uint64_t value() const { return value_; }

private:
    uint64_t value_;
};

class Host : public Header {
public:
    NAME("Host");

    void parse(const std::string& data);
    std::string host() const { return host_; }

private:
    std::string host_;
};

} // namespace Http

} // namespace Net

#undef NAME
