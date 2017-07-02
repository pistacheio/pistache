/* 
   Mathieu Stefani, 16 janvier 2016
   
   Representation of a Cookie as per http://tools.ietf.org/html/rfc6265
*/

#pragma once

#include <ctime>
#include <string>
#include <map>
#include <unordered_map>

#include <pistache/optional.h>
#include <pistache/http_defs.h>

namespace Pistache {
namespace Http {

struct Cookie {
    Cookie(std::string name, std::string value);

    std::string name;
    std::string value;

    Optional<std::string> path;
    Optional<std::string> domain;
    Optional<FullDate> expires;

    Optional<int> maxAge;
    bool secure;
    bool httpOnly;

    std::map<std::string, std::string> ext;

    static Cookie fromRaw(const char* str, size_t len);
    static Cookie fromString(const std::string& str);

    void write(std::ostream& os) const;
};

class CookieJar {
public:
    typedef std::unordered_map<std::string, Cookie> Storage;

    struct iterator : std::iterator<std::bidirectional_iterator_tag, Cookie> {
        iterator(const Storage::const_iterator& iterator)
            : it_(iterator)
        { }

        Cookie operator*() const {
            return it_->second;
        }

        iterator operator++() {
            ++it_;
            return iterator(it_);
        }

        iterator operator++(int) {
            iterator ret(it_);
            it_++;
            return ret;
        }

        bool operator !=(iterator other) const {
            return it_ != other.it_;
        }

        bool operator==(iterator other) const {
            return it_ == other.it_;
        }

    private:
        Storage::const_iterator it_;
    };

    CookieJar();

    void add(const Cookie& cookie);
    Cookie get(const std::string& name) const;

    bool has(const std::string& name) const;

    iterator begin() const {
        return iterator(cookies.begin());
    }

    iterator end() const {
        return iterator(cookies.end());
    }

private:
    Storage cookies;
};

} // namespace Net
} // namespace Pistache
