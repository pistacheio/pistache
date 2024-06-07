/*
 * SPDX-FileCopyrightText: 2015 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* http_headers.cc
   Mathieu Stefani, 19 August 2015

   Headers registry
*/

#include <pistache/http_headers.h>

#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace Pistache::Http::Header
{
    RegisterHeader(Accept);
    RegisterHeader(AccessControlAllowOrigin);
    RegisterHeader(AccessControlAllowHeaders);
    RegisterHeader(AccessControlExposeHeaders);
    RegisterHeader(AccessControlAllowMethods);
    RegisterHeader(Allow);
    RegisterHeader(CacheControl);
    RegisterHeader(Connection);
    RegisterHeader(AcceptEncoding);
    RegisterHeader(ContentEncoding);
    RegisterHeader(TransferEncoding);
    RegisterHeader(ContentLength);
    RegisterHeader(ContentType);
    RegisterHeader(Authorization);
    RegisterHeader(Date);
    RegisterHeader(Expect);
    RegisterHeader(Host);
    RegisterHeader(LastModified);
    RegisterHeader(Location);
    RegisterHeader(Server);
    RegisterHeader(UserAgent);

    bool strToQvalue(const char* str, float* qvalue, std::size_t* qvalueLen)
    {
        constexpr char offset = '0';

        *qvalueLen = 0;

        // It is useless to read more than 6 chars, as the maximum allowed
        // number of digits after the dot is 3, so n.nnn is 5.
        // The 6th character is read to check if the user specified a qvalue
        // with too many digits.
        for (; *qvalueLen < 6; (*qvalueLen)++)
        {
            // the decimal dot is only allowed at index 1;
            // 0.15  ok
            // 1.10  ok
            // 1.0.1 no
            // .40   no
            if (str[*qvalueLen] == '.' && *qvalueLen != 1)
            {
                return false;
            }

            // The only valid characters are digits and the decimal dot,
            // anything else signals the end of the string
            if (str[*qvalueLen] != '.' && !std::isdigit(str[*qvalueLen]))
            {
                break;
            }
        }

        // Guards against numbers like:
        // empty
        // 1.
        // 0.1234
        if (*qvalueLen < 1 || *qvalueLen == 2 || *qvalueLen > 5)
        {
            return false;
        }

        // The first char can only be 0 or 1
        if (str[0] != '0' && str[0] != '1')
        {
            return false;
        }

        int qint = 0;

        switch (*qvalueLen)
        {
        case 5:
            qint += (str[4] - offset);
            [[fallthrough]];
        case 4:
            qint += (str[3] - offset) * 10;
            [[fallthrough]];
        case 3:
            qint += (str[2] - offset) * 100;
            [[fallthrough]];
        case 1:
            qint += (str[0] - offset) * 1000;
        }

        *qvalue = static_cast<short>(qint) / 1000.0F;

        if (*qvalue > 1)
        {
            return false;
        }

        return true;
    }

    std::string toLowercase(std::string str)
    {
        std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        return str;
    }

    bool LowercaseEqualStatic(const std::string& dynamic,
                              const std::string& statik)
    {
        return std::equal(
            dynamic.begin(), dynamic.end(), statik.begin(), statik.end(),
            [](const char& a, const char& b) { return std::tolower(a) == b; });
    }

    Registry& Registry::instance()
    {
        static Registry instance;

        return instance;
    }

    Registry::Registry() = default;

    Registry::~Registry() = default;

    void Registry::registerHeader(const std::string& name,
                                  Registry::RegistryFunc func)
    {
        auto it = registry.find(name);
        if (it != std::end(registry))
        {
            throw std::runtime_error("Header already registered");
        }

        registry.insert(std::make_pair(name, std::move(func)));
    }

    std::vector<std::string> Registry::headersList()
    {
        std::vector<std::string> names;
        names.reserve(registry.size());

        for (const auto& header : registry)
        {
            names.push_back(header.first);
        }

        return names;
    }

    std::unique_ptr<Header> Registry::makeHeader(const std::string& name)
    {
        auto it = registry.find(name);
        if (it == std::end(registry))
        {
            throw std::runtime_error("Unknown header");
        }

        return it->second();
    }

    bool Registry::isRegistered(const std::string& name)
    {
        auto it = registry.find(name);
        return it != std::end(registry);
    }

    Collection& Collection::add(const std::shared_ptr<Header>& header)
    {
        headers.insert(std::make_pair(header->name(), header));

        return *this;
    }

    Collection& Collection::addRaw(const Raw& raw)
    {
        rawHeaders.insert(std::make_pair(raw.name(), raw));
        return *this;
    }

    std::shared_ptr<const Header> Collection::get(const std::string& name) const
    {
        auto header = getImpl(name);
        if (!header.first)
        {
            throw std::runtime_error("Could not find header");
        }

        return header.second;
    }

    std::shared_ptr<Header> Collection::get(const std::string& name)
    {
        auto header = getImpl(name);
        if (!header.first)
        {
            throw std::runtime_error("Could not find header");
        }

        return header.second;
    }

    Raw Collection::getRaw(const std::string& name) const
    {
        auto it = rawHeaders.find(name);
        if (it == std::end(rawHeaders))
        {
            throw std::runtime_error("Could not find header");
        }

        return it->second;
    }

    std::shared_ptr<const Header>
    Collection::tryGet(const std::string& name) const
    {
        auto header = getImpl(name);
        if (!header.first)
            return nullptr;

        return header.second;
    }

    std::shared_ptr<Header> Collection::tryGet(const std::string& name)
    {
        auto header = getImpl(name);
        if (!header.first)
            return nullptr;

        return header.second;
    }

    std::optional<Raw> Collection::tryGetRaw(const std::string& name) const
    {
        auto it = rawHeaders.find(name);
        if (it == std::end(rawHeaders))
        {
            return std::nullopt;
        }

        return std::optional<Raw>(it->second);
    }

    bool Collection::has(const std::string& name) const
    {
        return getImpl(name).first;
    }

    std::vector<std::shared_ptr<Header>> Collection::list() const
    {
        std::vector<std::shared_ptr<Header>> ret;
        ret.reserve(headers.size());
        for (const auto& h : headers)
        {
            ret.push_back(h.second);
        }

        return ret;
    }

    bool Collection::remove(const std::string& name)
    {
        auto tit = headers.find(name);
        if (tit == std::end(headers))
        {
            auto rit = rawHeaders.find(name);
            if (rit == std::end(rawHeaders))
                return false;

            rawHeaders.erase(rit);
            return true;
        }
        headers.erase(tit);
        return true;
    }

    void Collection::clear()
    {
        headers.clear();
        rawHeaders.clear();
    }

    std::pair<bool, std::shared_ptr<Header>>
    Collection::getImpl(const std::string& name) const
    {
        auto it = headers.find(name);
        if (it == std::end(headers))
        {
            return std::make_pair(false, nullptr);
        }

        return std::make_pair(true, it->second);
    }

} // namespace Pistache::Http::Header
