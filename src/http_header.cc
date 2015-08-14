/* http_header.cc
   Mathieu Stefani, 19 August 2015
   
   Implementation of common HTTP headers described by the RFC
*/

#include "http_header.h"
#include <stdexcept>

using namespace std;

namespace Net {

namespace Http {

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
Host::parse(const std::string& data) {
    host_ = data;
}

} // namespace Http

} // namespace Net
