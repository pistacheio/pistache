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
