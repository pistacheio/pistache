/* 
   Mathieu Stefani, 16 janvier 2016
   
   Representation of a Cookie as per http://tools.ietf.org/html/rfc6265
*/

#pragma once

#include <ctime>
#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <list>

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
    typedef std::unordered_map<std::string, Cookie> HashMapCookies; // "value" -> Cookie  
    typedef std::unordered_map<std::string, HashMapCookies> Storage; // "name" -> Hashmap("value" -> Cookie)

    struct iterator : std::iterator<std::bidirectional_iterator_tag, Cookie> {
        iterator(const Storage::const_iterator& _iterator)
            : it_(_iterator)
        {  
        }
        
        iterator(const Storage::const_iterator& _iterator, const Storage::const_iterator& end)
            : it_(_iterator),end_(end)
        {   
            if(it_ != end_) {
                it_2 = it_->second.begin();
            }
        }

        Cookie operator*() const {
            return it_2->second; // return it_->second;
        }
        
        iterator operator++() {
            ++it_2;
            if(it_2 == it_->second.end()) {
                ++it_;
                if(it_ != end_)
                    it_2 = it_->second.begin();
            }
            return iterator(it_,end_);
        }

        iterator operator++(int) {
            iterator ret(it_,end_);
            ++it_2;
            if(it_2 == it_->second.end()) {
                ++it_;
                if(it_ != end_)   // this check is important
                    it_2 = it_->second.begin();
            }

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
        HashMapCookies::const_iterator it_2;
        Storage::const_iterator end_; // we need to know where main hashmap ends. 
    };

    CookieJar();

    void add(const Cookie& cookie);
    void removeCookie(const std::string& name);  // ADDED LATER
    
    void addFromRaw(const char *str, size_t len);
    Cookie get(const std::string& name) const;

    bool has(const std::string& name) const;

    iterator begin() const {
        return iterator(cookies.begin(),cookies.end());
    }

    iterator end() const {
        return iterator(cookies.end());
    }

private:
    Storage cookies;
};

} // namespace Net
} // namespace Pistache
