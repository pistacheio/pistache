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

class Headers {
public:

    template<typename H>
    /*
    typename std::enable_if<
                 std::is_base_of<H, Header>::value, std::shared_ptr<Header>
             >::type
     */
    std::shared_ptr<H>
    getHeader() const {
        return std::static_pointer_cast<H>(getHeader(H::Name));
    }

    void add(const std::shared_ptr<Header>& header);

    std::shared_ptr<Header> getHeader(const std::string& name) const;

private:
    std::unordered_map<std::string, std::shared_ptr<Header>> headers;
};

struct HeaderRegistry {

    typedef std::function<std::unique_ptr<Header>()> RegistryFunc;

    template<typename H>
    static
    /* typename std::enable_if<
                std::is_base_of<H, Header>::value, void
             >::type
             */
    void
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

} // namespace Http

} // namespace Net
