/* http_headers.h
   Mathieu Stefani, 19 August 2015
   
   A list of HTTP headers
*/

#pragma once

#include <unordered_map>
#include <vector>
#include <memory>
#include "http_header.h"

namespace Net {

namespace Http {

namespace Header {

class Collection {
public:

    template<typename H>
    typename std::enable_if<
                 IsHeader<H>::value, std::shared_ptr<const H>
             >::type
    get() const {
        return std::static_pointer_cast<const H>(get(H::Name));
    }
    template<typename H>
    typename std::enable_if<
                 IsHeader<H>::value, std::shared_ptr<H>
             >::type
    get() {
        return std::static_pointer_cast<H>(get(H::Name));
    }

    template<typename H>
    typename std::enable_if<
                 IsHeader<H>::value, std::shared_ptr<const H>
             >::type
    tryGet() const {
        return std::static_pointer_cast<const H>(tryGet(H::Name));
    }
    template<typename H>
    typename std::enable_if<
                 IsHeader<H>::value, std::shared_ptr<H>
             >::type
    tryGet() {
        return std::static_pointer_cast<H>(tryGet(H::Name));
    }

    Collection& add(const std::shared_ptr<Header>& header);

    std::shared_ptr<const Header> get(const std::string& name) const;
    std::shared_ptr<Header> get(const std::string& name);

    std::shared_ptr<const Header> tryGet(const std::string& name) const;
    std::shared_ptr<Header> tryGet(const std::string& name);

    template<typename H>
    typename std::enable_if<
                 IsHeader<H>::value, bool
             >::type
    has() const {
        return has(H::Name);
    }
    bool has(const std::string& name) const;

    std::vector<std::shared_ptr<Header>> list() const;

    void clear();

private:
    std::pair<bool, std::shared_ptr<Header>> getImpl(const std::string& name) const;

    std::unordered_map<std::string, std::shared_ptr<Header>> headers;
};

struct Registry {

    typedef std::function<std::unique_ptr<Header>()> RegistryFunc;

    template<typename H>
    static
    typename std::enable_if<
                IsHeader<H>::value, void
             >::type
    registerHeader() {
        registerHeader(H::Name, []() -> std::unique_ptr<Header> {
            return std::unique_ptr<Header>(new H());
        });

    }

    static void registerHeader(std::string name, RegistryFunc func);

    static std::vector<std::string> headersList();

    static std::unique_ptr<Header> makeHeader(const std::string& name);
    static bool isRegistered(const std::string& name);
};

} // namespace Header

} // namespace Http

} // namespace Net
