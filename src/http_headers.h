/* http_headers.h
   Mathieu Stefani, 19 August 2015
   
   A list of HTTP headers
*/

#pragma once

#include <unordered_map>
#include <vector>
#include <memory>
#include "http_header.h"
#include "MemoryPool.h"
#include <mutex>
#include <array>

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
    Collection& add(std::shared_ptr<Header>&& header);
    Collection& addRaw(const Raw& raw);

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

    void clear();

private:
    std::pair<bool, std::shared_ptr<Header>> getImpl(const std::string& name) const;

    std::unordered_map<std::string, std::shared_ptr<Header>> headers;
    std::unordered_map<std::string, Raw> rawHeaders;
};

struct header_delete {
    void operator()(Header* header);
};

struct AllocatorBase {
    virtual Header* allocate(uint64_t tid) = 0;
    virtual Header* defaultConstruct(uint64_t tid) = 0;

    virtual void deallocate(Header* header, uint64_t tid) = 0;

    virtual ~AllocatorBase() { }
};

template<typename H>
struct Allocator : public AllocatorBase {
public:
    Allocator()
    {
    }

    Header* allocate(uint64_t tid) {
        return typedAllocate(tid);
    }

    Header* defaultConstruct(uint64_t tid) {
        H *header = typedAllocate(tid);
        new (header) H();
        return header;
    }

    void deallocate(Header* header, uint64_t tid) {
        auto& stripe = getStripe(tid);

        typename Stripe::Guard guard(stripe.lock);
        stripe.pool.deleteElement(static_cast<H *>(header));
    }

private:
    struct Stripe {
        typedef std::mutex Lock;
        typedef std::lock_guard<Lock> Guard;

        Lock lock;
        MemoryPool<H> pool;
    };

    std::array<Stripe, 24> stripes;

    Stripe& getStripe(uint64_t tid) {
        return stripes[tid % stripes.size()];
    }

    H* typedAllocate(uint64_t tid) {
        auto& stripe = getStripe(tid);

        typename Stripe::Guard guard(stripe.lock);
        return stripe.pool.allocate();
    }
};

struct Registry {

    template<typename H>
    static
    typename std::enable_if<
                IsHeader<H>::value, void
             >::type
    registerHeader() {
        auto alloc = std::unique_ptr<AllocatorBase>(new Allocator<H>());
        registerHeader(H::Name, std::move(alloc));
    }

    static void registerHeader(std::string name, std::unique_ptr<AllocatorBase>&& alloc);

    static std::vector<std::string> headersList();

    static std::unique_ptr<Header, header_delete> makeHeader(const std::string& name);
    static bool isRegistered(const std::string& name);
};

template<typename H>
struct Registrar {
    static_assert(IsHeader<H>::value, "Registrar only works with header types");

    Registrar() {
        Registry::registerHeader<H>();
    }
};

namespace detail {
    Header* allocate_header(const std::string& name);
}

/* Crazy macro machinery to generate a unique variable name
 * Don't touch it !
 */
#define CAT(a, b) CAT_I(a, b)
#define CAT_I(a, b) a ## b

#define UNIQUE_NAME(base) CAT(base, __LINE__)

#define RegisterHeader(Header) \
    Registrar<Header> UNIQUE_NAME(CAT(CAT_I(__, Header), __))

} // namespace Header

template<typename H, typename... Args>
typename std::enable_if<
    Header::IsHeader<H>::value,
    std::unique_ptr<H, Header::header_delete>
>::type
make_header(Args&&... args) {

    auto h = static_cast<H *>(Header::detail::allocate_header(H::Name));
    new (h) H(std::forward<Args>(args)...);

    return std::unique_ptr<H, Header::header_delete>(h);
}

} // namespace Http

} // namespace Net
