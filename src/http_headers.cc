/* http_headers.cc
   Mathieu Stefani, 19 August 2015
   
   Headers registry
*/

#include "http_headers.h"
#include <unordered_map>
#include <iterator>
#include <stdexcept>
#include <iostream>

namespace Net {

namespace Http {

namespace {
    std::unordered_map<std::string, HeaderRegistry::RegistryFunc> registry;
}

void
HeaderRegistry::registerHeader(std::string name, HeaderRegistry::RegistryFunc func)
{
    auto it = registry.find(name);
    if (it != std::end(registry)) {
        throw std::runtime_error("Header already registered");
    }

    registry.insert(std::make_pair(name, std::move(func)));
}

std::vector<std::string>
HeaderRegistry::headersList() {
    std::vector<std::string> names;
    names.reserve(registry.size());

    for (const auto &header: registry) {
        names.push_back(header.first);
    }

    return names;
}

std::unique_ptr<Header>
HeaderRegistry::makeHeader(const std::string& name) {
    auto it = registry.find(name);
    if (it == std::end(registry)) {
        throw std::runtime_error("Unknown header");
    }

    return it->second();
}

bool
HeaderRegistry::isRegistered(const std::string& name) {
    auto it = registry.find(name);
    return it != std::end(registry);
}

Headers&
Headers::add(const std::shared_ptr<Header>& header) {
    headers.insert(std::make_pair(header->name(), header));
    
    return *this;
}

std::shared_ptr<const Header>
Headers::get(const std::string& name) const {
    auto header = getImpl(name);
    if (!header.first) {
        throw std::runtime_error("Could not find header");
    }

    return header.second;
}

std::shared_ptr<Header>
Headers::get(const std::string& name) {
    auto header = getImpl(name);
    if (!header.first) {
        throw std::runtime_error("Could not find header");
    }

    return header.second;
}

std::shared_ptr<const Header>
Headers::tryGet(const std::string& name) const {
    auto header = getImpl(name);
    if (!header.first) return nullptr;

    return header.second;
}

std::shared_ptr<Header>
Headers::tryGet(const std::string& name) {
    auto header = getImpl(name);
    if (!header.first) return nullptr;

    return header.second;
}

bool
Headers::has(const std::string& name) const {
    return getImpl(name).first;
}

std::vector<std::shared_ptr<Header>>
Headers::list() const {
    std::vector<std::shared_ptr<Header>> ret;
    ret.reserve(headers.size());
    for (const auto& h: headers) {
        ret.push_back(h.second);
    }

    return ret;
}

std::pair<bool, std::shared_ptr<Header>>
Headers::getImpl(const std::string& name) const {
    auto it = headers.find(name);
    if (it == std::end(headers)) {
        return std::make_pair(false, nullptr);
    }

    return std::make_pair(true, it->second);
}

namespace {
    struct AtInit {
        AtInit() {
            HeaderRegistry::registerHeader<ContentLength>();
            HeaderRegistry::registerHeader<Host>();
            HeaderRegistry::registerHeader<Accept>();
            HeaderRegistry::registerHeader<UserAgent>();
            HeaderRegistry::registerHeader<ContentEncoding>();
        }
    } atInit;
}

} // namespace Http

} // namespace Net
