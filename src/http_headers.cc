/* http_headers.cc
   Mathieu Stefani, 19 August 2015
   
   Headers registry
*/

#include "http_headers.h"
#include <unordered_map>
#include <iterator>
#include <stdexcept>
#include <iostream>
#include <atomic>

namespace Net {

namespace Http {

namespace Header {

namespace {
    std::atomic<uint64_t> g_threadCount { 0 };
    __thread uint64_t tid = 0;

    uint64_t thread_id() {
        if (tid == 0) {
            tid = g_threadCount.fetch_add(1);
        }

        return tid;
    }

    std::unordered_map<string, std::unique_ptr<AllocatorBase>> allocators;

}

namespace detail {
    Header *
    allocate_header(const string& name) {
        auto it = allocators.find(name);
        if (it == std::end(allocators)) {
            throw std::bad_alloc();
        }

        return it->second->allocate(thread_id());
    }
}

void
header_delete::operator()(Header* header) {
    auto it = allocators.find(header->name());

    it->second->deallocate(header, thread_id());
}

RegisterHeader(Accept);
RegisterHeader(CacheControl);
RegisterHeader(Connection);
RegisterHeader(ContentEncoding);
RegisterHeader(ContentLength);
RegisterHeader(ContentType);
RegisterHeader(Date);
RegisterHeader(Host);
RegisterHeader(Server);
RegisterHeader(UserAgent);

void
Registry::registerHeader(string name, std::unique_ptr<AllocatorBase>&& alloc)
{
    allocators.insert(std::make_pair(name, std::move(alloc)));
}

std::vector<string>
Registry::headersList() {
    std::vector<string> names;
    names.reserve(allocators.size());

    for (const auto &header: allocators) {
        names.push_back(header.first);
    }

    return names;
}

std::unique_ptr<Header, header_delete>
Registry::makeHeader(const string& name) {
    auto it = allocators.find(name);
    if (it == std::end(allocators)) {
        throw std::bad_alloc();
    }
    auto h = it->second->defaultConstruct(thread_id());
    return std::unique_ptr<Header, header_delete>(h);
}

bool
Registry::isRegistered(const string& name) {
    auto it = allocators.find(name);
    return it != std::end(allocators);
}

Collection&
Collection::add(const std::shared_ptr<Header>& header) {
    headers.insert(std::make_pair(header->name(), header));
    
    return *this;
}
Collection&
Collection::add(std::shared_ptr<Header>&& header) {
    headers.insert(std::make_pair(header->name(), std::move(header)));

    return *this;
}

Collection&
Collection::addRaw(const Raw& raw) {
    //rawHeaders.insert(std::make_pair(raw.name(), raw));
}

std::shared_ptr<const Header>
Collection::get(const string& name) const {
    auto header = getImpl(name);
    if (!header.first) {
        throw std::runtime_error("Could not find header");
    }

    return header.second;
}

std::shared_ptr<Header>
Collection::get(const string& name) {
    auto header = getImpl(name);
    if (!header.first) {
        throw std::runtime_error("Could not find header");
    }

    return header.second;
}

Raw
Collection::getRaw(const string& name) const {
    auto it = rawHeaders.find(name);
    if (it == std::end(rawHeaders)) {
        throw std::runtime_error("Could not find header");
    }

    return it->second;
}

std::shared_ptr<const Header>
Collection::tryGet(const string& name) const {
    auto header = getImpl(name);
    if (!header.first) return nullptr;

    return header.second;
}

std::shared_ptr<Header>
Collection::tryGet(const string& name) {
    auto header = getImpl(name);
    if (!header.first) return nullptr;

    return header.second;
}

Optional<Raw>
Collection::tryGetRaw(const string& name) const {
    auto it = rawHeaders.find(name);
    if (it == std::end(rawHeaders)) {
        return None();
    }

    return Some(it->second);
}

bool
Collection::has(const string& name) const {
    return getImpl(name).first;
}

std::vector<std::shared_ptr<Header>>
Collection::list() const {
    std::vector<std::shared_ptr<Header>> ret;
    ret.reserve(headers.size());
    for (const auto& h: headers) {
        ret.push_back(h.second);
    }

    return ret;
}

void
Collection::clear() {
    headers.clear();
    rawHeaders.clear();
}

std::pair<bool, std::shared_ptr<Header>>
Collection::getImpl(const string& name) const {
    auto it = headers.find(name);
    if (it == std::end(headers)) {
        return std::make_pair(false, nullptr);
    }

    return std::make_pair(true, it->second);
}


} // namespace Header

} // namespace Http

} // namespace Net
