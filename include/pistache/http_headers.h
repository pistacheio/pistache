/* http_headers.h
   Mathieu Stefani, 19 August 2015

   A list of HTTP headers
*/

#pragma once

#include <functional>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <memory>

#include <pistache/http_header.h>

namespace Pistache {
namespace Http {
namespace Header {

std::string
toLowercase(std::string str);

struct LowercaseHash {
    size_t operator()(const std::string& key) const {
        return std::hash<std::string>{}(toLowercase(key));
    }
};

struct LowercaseEqual {
    bool operator()(const std::string& left, const std::string& right) const {
        return toLowercase(left) == toLowercase(right);
    }
};

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
    Collection& addRaw(const Raw& raw);

    template<typename H, typename ...Args>
    typename std::enable_if<
                IsHeader<H>::value, Collection&
             >::type
    add(Args&& ...args) {
        return add(std::make_shared<H>(std::forward<Args>(args)...));
    }

    template<typename H>
    typename std::enable_if<
                IsHeader<H>::value, bool
             >::type
    remove() {
        return remove(H::Name);
    }

    std::shared_ptr<const Header> get(const std::string& name) const;
    std::shared_ptr<Header> get(const std::string& name);
    Raw getRaw(const std::string& name) const;

    std::shared_ptr<const Header> tryGet(const std::string& name) const;
    std::shared_ptr<Header> tryGet(const std::string& name);
    Optional<Raw> tryGetRaw(const std::string& name) const;

    template<typename H>
    typename std::enable_if<
                 IsHeader<H>::value, bool
             >::type
    has() const {
        return has(H::Name);
    }
    bool has(const std::string& name) const;

    std::vector<std::shared_ptr<Header>> list() const;

    const std::unordered_map<std::string, Raw>& rawList() const {
        return rawHeaders;
    }

    bool remove(const std::string& name);

    void clear();

private:
    std::pair<bool, std::shared_ptr<Header>> getImpl(const std::string& name) const;

    std::unordered_map<
        std::string,
        std::shared_ptr<Header>,
        LowercaseHash,
        LowercaseEqual
    > headers;
    std::unordered_map<std::string, Raw> rawHeaders;
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

template<typename H>
struct Registrar {
    static_assert(IsHeader<H>::value, "Registrar only works with header types");

    Registrar() {
        Registry::registerHeader<H>();
    }
};

/* Crazy macro machinery to generate a unique variable name
 * Don't touch it !
 */
#define CAT(a, b) CAT_I(a, b)
#define CAT_I(a, b) a ## b

#define UNIQUE_NAME(base) CAT(base, __LINE__)

#define RegisterHeader(Header) \
    Registrar<Header> UNIQUE_NAME(CAT(CAT_I(__, Header), __))


} // namespace Header
} // namespace Http
} // namespace Pistache
