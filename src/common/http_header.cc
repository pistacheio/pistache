/*
 * SPDX-FileCopyrightText: 2015 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* http_header.cc
   Mathieu Stefani, 19 August 2015

   Implementation of common HTTP headers described by the RFC
*/

#include <pistache/base64.h>
#include <pistache/common.h>
#include <pistache/config.h>
#include <pistache/http.h>
#include <pistache/http_header.h>
#include <pistache/stream.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>

namespace Pistache::Http::Header
{

    void Header::parse(const std::string& data)
    {
        parseRaw(data.c_str(), data.length());
    }

    void Header::parseRaw(const char* str, size_t len)
    {
        parse(std::string(str, len));
    }

    void Allow::parseRaw(const char* /*str*/, size_t /*len*/)
    {
    }

    const char* encodingString(Encoding encoding)
    {
        switch (encoding)
        {
        case Encoding::Gzip:
            return "gzip";
        case Encoding::Br:
            return "br";
        case Encoding::Compress:
            return "compress";
        case Encoding::Deflate:
            return "deflate";
        case Encoding::Identity:
            return "identity";
        case Encoding::Chunked:
            return "chunked";
        case Encoding::Unknown:
            return "unknown";
        }
        return "unknown";
    }

    Encoding encodingFromString(const std::string_view str)
    {
        if (str.empty())
        {
            return Encoding::Unknown;
        }

        if (!strncasecmp(str.data(), "gzip", str.length()))
        {
            return Encoding::Gzip;
        }
        else if (!strncasecmp(str.data(), "br", str.length()))
        {
            return Encoding::Br;
        }
        else if (!strncasecmp(str.data(), "deflate", str.length()))
        {
            return Encoding::Deflate;
        }
        else if (!strncasecmp(str.data(), "compress", str.length()))
        {
            return Encoding::Compress;
        }
        else if (!strncasecmp(str.data(), "identity", str.length()))
        {
            return Encoding::Identity;
        }
        else if (!strncasecmp(str.data(), "chunked", str.length()))
        {
            return Encoding::Chunked;
        }
        else
        {
            return Encoding::Unknown;
        }
    }

    bool encodingSupported(const Encoding encoding)
    {
        switch (encoding)
        {
#ifdef PISTACHE_USE_CONTENT_ENCODING_BROTLI
        case Encoding::Br:
            /* @fallthrough@ */
#endif
#ifdef PISTACHE_USE_CONTENT_ENCODING_DEFLATE
        case Encoding::Deflate:
            /* @fallthrough@ */
#endif
        case Encoding::Identity:
            return true;
        default:
            return false;
        }
    }

    void Allow::write(std::ostream& os) const
    {
        /* This puts an extra ',' at the end :/
  std::copy(std::begin(methods_), std::end(methods_),
            std::ostream_iterator<Http::Method>(os, ", "));
  */

        for (std::vector<Http::Method>::size_type i = 0; i < methods_.size(); ++i)
        {
            os << methods_[i];
            if (i < methods_.size() - 1)
                os << ", ";
        }
    }

    void Allow::addMethod(Http::Method method) { methods_.push_back(method); }

    void Allow::addMethods(std::initializer_list<Method> methods)
    {
        std::copy(std::begin(methods), std::end(methods),
                  std::back_inserter(methods_));
    }

    void Allow::addMethods(const std::vector<Http::Method>& methods)
    {
        std::copy(std::begin(methods), std::end(methods),
                  std::back_inserter(methods_));
    }

    CacheControl::CacheControl(Http::CacheDirective directive)
    {
        directives_.push_back(directive);
    }

    void CacheControl::parseRaw(const char* str, size_t len)
    {
        using Http::CacheDirective;

        struct DirectiveValue
        {
            const char* const str;
            const size_t size;
            CacheDirective::Directive repr;
        };

#define VALUE(divStr, enumValue)                              \
    {                                                         \
        divStr, sizeof(divStr) - 1, CacheDirective::enumValue \
    }

        static constexpr DirectiveValue TrivialDirectives[] = {
            VALUE("no-cache", NoCache),
            VALUE("no-store", NoStore),
            VALUE("no-transform", NoTransform),
            VALUE("only-if-cached", OnlyIfCached),
            VALUE("public", Public),
            VALUE("private", Private),
            VALUE("must-revalidate", MustRevalidate),
            VALUE("proxy-revalidate", ProxyRevalidate)
        };

        static constexpr DirectiveValue TimedDirectives[] = {
            VALUE("max-age", MaxAge), VALUE("max-stale", MaxStale),
            VALUE("min-fresh", MinFresh), VALUE("s-maxage", SMaxAge)
        };

#undef VALUE

        RawStreamBuf<> buf(const_cast<char*>(str), len);
        StreamCursor cursor(&buf);

        do
        {

            bool found = false;
            // First scan trivial directives
            for (const auto& d : TrivialDirectives)
            {
                if (match_raw(d.str, d.size, cursor))
                {
                    directives_.emplace_back(d.repr);
                    found = true;
                    break;
                }
            }

            // Not found, let's try timed directives
            if (!found)
            {
                for (const auto& d : TimedDirectives)
                {
                    if (match_raw(d.str, d.size, cursor))
                    {
                        if (!cursor.advance(1))
                        {
                            throw std::runtime_error(
                                "Invalid caching directive, missing delta-seconds");
                        }

                        char* end;
                        const char* beg = cursor.offset();
                        // @Security: if str is not \0 terminated, there might be a situation
                        // where strtol can overflow. Double-check that it's harmless and fix
                        // if not
                        auto secs = strtol(beg, &end, 10);
                        cursor.advance(end - beg);
                        if (!cursor.eof() && cursor.current() != ',')
                        {
                            throw std::runtime_error(
                                "Invalid caching directive, malformated delta-seconds");
                        }
                        directives_.emplace_back(d.repr, std::chrono::seconds(secs));
                        break;
                    }
                }
            }

            if (!cursor.eof())
            {
                if (cursor.current() != ',')
                {
                    throw std::runtime_error("Invalid caching directive, expected a comma");
                }

                int c;
                while ((c = cursor.current()) != StreamCursor::Eof && (c == ',' || c == ' '))
                    cursor.advance(1);
            }

        } while (!cursor.eof());
    }

    void CacheControl::write(std::ostream& os) const
    {
        using Http::CacheDirective;

        auto directiveString = [](CacheDirective directive) -> const char* {
            switch (directive.directive())
            {
            case CacheDirective::NoCache:
                return "no-cache";
            case CacheDirective::NoStore:
                return "no-store";
            case CacheDirective::NoTransform:
                return "no-transform";
            case CacheDirective::OnlyIfCached:
                return "only-if-cached";
            case CacheDirective::Public:
                return "public";
            case CacheDirective::Private:
                return "private";
            case CacheDirective::MustRevalidate:
                return "must-revalidate";
            case CacheDirective::ProxyRevalidate:
                return "proxy-revalidate";
            case CacheDirective::MaxAge:
                return "max-age";
            case CacheDirective::MaxStale:
                return "max-stale";
            case CacheDirective::MinFresh:
                return "min-fresh";
            case CacheDirective::SMaxAge:
                return "s-maxage";
            case CacheDirective::Ext:
                return "";
            default:
                return "";
            }
            return "";
        };

        auto hasDelta = [](CacheDirective directive) {
            switch (directive.directive())
            {
            case CacheDirective::MaxAge:
            case CacheDirective::MaxStale:
            case CacheDirective::MinFresh:
            case CacheDirective::SMaxAge:
                return true;
            default:
                return false;
            }
        };

        for (std::vector<CacheDirective>::size_type i = 0; i < directives_.size();
             ++i)
        {
            const auto& d = directives_[i];
            os << directiveString(d);
            if (hasDelta(d))
            {
                auto delta = d.delta();
                if (delta.count() > 0)
                {
                    os << "=" << delta.count();
                }
            }

            if (i < directives_.size() - 1)
            {
                os << ", ";
            }
        }
    }

    void CacheControl::addDirective(Http::CacheDirective directive)
    {
        directives_.push_back(directive);
    }

    void CacheControl::addDirectives(
        const std::vector<Http::CacheDirective>& directives)
    {
        std::copy(std::begin(directives), std::end(directives),
                  std::back_inserter(directives_));
    }

    void Connection::parseRaw(const char* str, size_t len)
    {
        char* p = const_cast<char*>(str);
        RawStreamBuf<> buf(p, p + len);
        StreamCursor cursor(&buf);

        if (match_string("close", cursor))
        {
            control_ = ConnectionControl::Close;
        }
        else if (match_string("keep-alive", cursor))
        {
            control_ = ConnectionControl::KeepAlive;
        }
        else
        {
            control_ = ConnectionControl::Ext;
        }
    }

    void Connection::write(std::ostream& os) const
    {
        switch (control_)
        {
        case ConnectionControl::Close:
            os << "Close";
            break;
        case ConnectionControl::KeepAlive:
            os << "Keep-Alive";
            break;
        case ConnectionControl::Ext:
            os << "Ext";
            break;
        }
    }

    void ContentLength::parse(const std::string& data)
    {
        try
        {
            size_t pos;
            uint64_t val = std::stoull(data, &pos);
            if (pos != 0)
            {
            }

            value_ = val;
        }
        catch (const std::invalid_argument& /*e*/)
        {
        }
    }

    void ContentLength::write(std::ostream& os) const { os << value_; }

    // What type of authorization method was used?
    Authorization::Method Authorization::getMethod() const noexcept
    {
        // Basic...
        if (hasMethod<Method::Basic>())
            return Method::Basic;

        // Bearer...
        else if (hasMethod<Method::Bearer>())
            return Method::Bearer;

        // Unknown...
        else
            return Method::Unknown;
    }

    // Authorization is basic method...
    template <>
    bool Authorization::hasMethod<Authorization::Method::Basic>() const noexcept
    {
        // Method should begin with "Basic: "
        if (value().rfind("Basic ", 0) == std::string::npos)
            return false;

        // Verify value is long enough to contain basic method's credentials...
        if (value().length() <= std::string("Basic ").length())
            return false;

        // Looks good...
        return true;
    }

    // Authorization is bearer method...
    template <>
    bool Authorization::hasMethod<Authorization::Method::Bearer>() const noexcept
    {
        // Method should begin with "Bearer: "
        if (value().rfind("Bearer ", 0) == std::string::npos)
            return false;

        // Verify value is long enough to contain basic method's credentials...
        if (value().length() <= std::string("Bearer ").length())
            return false;

        // Looks good...
        return true;
    }

    // Get decoded user ID if basic method was used...
    std::string Authorization::getBasicUser() const
    {
        // Verify basic authorization method was used...
        if (!hasMethod<Authorization::Method::Basic>())
            throw std::runtime_error("Authorization header does not use Basic method.");

        // Extract encoded credentials...
        const std::string EncodedCredentials(
            value_.begin() + std::string("Basic ").length(), value_.end());

        // Decode them...
        Base64Decoder Decoder(EncodedCredentials);
        const std::vector<std::byte>& BinaryDecodedCredentials = Decoder.Decode();

        // Transform to string...
        std::string DecodedCredentials;
        for (std::byte CurrentByte : BinaryDecodedCredentials)
            DecodedCredentials.push_back(static_cast<char>(CurrentByte));

        // Find user ID and password delimiter...
        const auto Delimiter = DecodedCredentials.find_first_of(':');

        // None detected. Assume this is a malformed header...
        if (Delimiter == std::string::npos)
            return std::string();

        // Extract and return just the user ID...
        return std::string(DecodedCredentials.begin(),
                           DecodedCredentials.begin() + Delimiter);
    }

    // Get decoded password if basic method was used...
    std::string Authorization::getBasicPassword() const
    {
        // Verify basic authorization method was used...
        if (!hasMethod<Authorization::Method::Basic>())
            throw std::runtime_error("Authorization header does not use Basic method.");

        // Extract encoded credentials...
        const std::string EncodedCredentials(
            value_.begin() + std::string("Basic ").length(), value_.end());

        // Decode them...
        Base64Decoder Decoder(EncodedCredentials);
        const std::vector<std::byte>& BinaryDecodedCredentials = Decoder.Decode();

        // Transform to string...
        std::string DecodedCredentials;
        for (std::byte CurrentByte : BinaryDecodedCredentials)
            DecodedCredentials.push_back(static_cast<char>(CurrentByte));

        // Find user ID and password delimiter...
        const auto Delimiter = DecodedCredentials.find_first_of(':');

        // None detected. Assume this is a malformed header...
        if (Delimiter == std::string::npos)
            return std::string();

        // Extract and return just the password...
        return std::string(DecodedCredentials.begin() + Delimiter + 1,
                           DecodedCredentials.end());
    }

    // Set encoded user ID and password for basic method...
    void Authorization::setBasicUserPassword(const std::string& User,
                                             const std::string& Password)
    {
        // Verify user ID does not contain a colon...
        if (User.find_first_of(':') != std::string::npos)
            throw std::runtime_error("User ID cannot contain a colon.");

        // Format credentials string...
        const std::string Credentials = User + std::string(":") + Password;

        // Encode credentials...
        value_ = std::string("Basic ") + Base64Encoder::EncodeString(Credentials);
    }

    void Authorization::parse(const std::string& data)
    {
        value_ = data;
    }

    void Authorization::write(std::ostream& os) const { os << value_; }

    void Date::parse(const std::string& str)
    {
        fullDate_ = FullDate::fromString(str);
    }

    void Date::write(std::ostream& os) const { fullDate_.write(os); }

    void Expect::parseRaw(const char* str, size_t /*len*/)
    {
        if (std::strcmp(str, "100-continue") == 0)
        {
            expectation_ = Expectation::Continue;
        }
        else
        {
            expectation_ = Expectation::Ext;
        }
    }

    void Expect::write(std::ostream& os) const
    {
        if (expectation_ == Expectation::Continue)
        {
            os << "100-continue";
        }
    }

    Host::Host(const std::string& host, Port port)
        : Host(host + ':' + std::to_string(port))
    { }

    Host::Host(const std::string& data)
    {
        parse(data);
    }

    void Host::parse(const std::string& data)
    {
        const AddressParser parser(data);

        /* AddressParser returns an IPv6 host address, but RFC 9112 requires
         * that the value of the "Host" header is an URI host, as defined in
         * RFC 3986 section 3.2.2 */
        if (parser.family() == AF_INET6)
        {
            uriHost_ = '[' + parser.rawHost() + ']';
        }
        else
        {
            uriHost_ = parser.rawHost();
        }

        const std::string& port = parser.rawPort();
        if (port.empty())
        {
            port_ = Const::HTTP_STANDARD_PORT;
        }
        else
        {
            port_ = Port(port);
        }
    }

    void Host::write(std::ostream& os) const
    {
        os << uriHost_;
        /* @Clarity @Robustness: maybe a found a literal different than zero
     to represent a null port ?
  */
        if (port_ != 0)
        {
            os << ":" << port_;
        }
    }

    void LastModified::parse(const std::string& data)
    {
        fullDate_ = FullDate::fromString(data);
    }

    void LastModified::write(std::ostream& os) const
    {
        fullDate_.write(os, FullDate::Type::RFC1123GMT);
    }

    Location::Location(const std::string& location)
        : location_(location)
    { }

    void Location::parse(const std::string& data) { location_ = data; }

    void Location::write(std::ostream& os) const { os << location_; }

    void UserAgent::parse(const std::string& data) { ua_ = data; }

    void UserAgent::write(std::ostream& os) const { os << ua_; }

    void Accept::parseRaw(const char* str, size_t len)
    {

        RawStreamBuf<char> buf(const_cast<char*>(str), len);
        StreamCursor cursor(&buf);

        do
        {
            int c;
            size_t beg = cursor;
            while ((c = cursor.next()) != StreamCursor::Eof && c != ',')
                cursor.advance(1);

            cursor.advance(1);

            const size_t mimeLen = cursor.diff(beg);

            mediaRange_.push_back(
                Mime::MediaType::fromRaw(cursor.offset(beg), mimeLen));

            if (!cursor.eof())
            {
                if (!cursor.advance(1))
                    throw std::runtime_error("Ill-formed Accept header");

                if ((c = cursor.next()) == StreamCursor::Eof || c == ',' || c == 0)
                    throw std::runtime_error("Ill-formed Accept header");

                while (!cursor.eof() && cursor.current() == ' ')
                    cursor.advance(1);
            }

        } while (!cursor.eof());
    }

    void Accept::write(std::ostream& os) const
    {
        if (mediaRange_.empty())
        {
            return;
        }
        for (size_t i = 0; i < mediaRange_.size() - 1; i++)
        {
            os << mediaRange_[i].toString() << ", ";
        }
        os << mediaRange_[mediaRange_.size() - 1].toString();
    }

    void AccessControlAllowOrigin::parse(const std::string& data) { uri_ = data; }

    void AccessControlAllowOrigin::write(std::ostream& os) const { os << uri_; }

    void AccessControlAllowHeaders::parse(const std::string& data) { val_ = data; }

    void AccessControlAllowHeaders::write(std::ostream& os) const { os << val_; }

    void AccessControlExposeHeaders::parse(const std::string& data) { val_ = data; }

    void AccessControlExposeHeaders::write(std::ostream& os) const { os << val_; }

    void AccessControlAllowMethods::parse(const std::string& data) { val_ = data; }

    void AccessControlAllowMethods::write(std::ostream& os) const { os << val_; }

    void EncodingHeader::parseRaw(const char* str, size_t len)
    {
        encoding_ = encodingFromString(std::string_view(str, len));
    }

    AcceptEncoding::AcceptEncoding(Encoding encoding)
    {
        insertEncoding(std::make_pair(encoding, 1.0F));
    }

    /*
     * Tokens are short textual identifiers that do not include whitespace or delimiters.
     *
     * tchar = "!" / "#" / "$" / "%" / "&" / "'" / "*"
     *       / "+" / "-" / "." / "^" / "_" / "`" / "|" / "~"
     *       / DIGIT / ALPHA
     */
    static bool is_http_token(const unsigned char c)
    {
        return c == '!' || c == '#' || c == '$' || c == '%' || c == '&' || c == '\''
            || c == '*' || c == '+' || c == '-' || c == '.' || c == '^' || c == '_'
            || c == '`' || c == '|' || c == '~' || std::isalnum(c);
    }

    static bool is_http_space(const unsigned char c)
    {
        return std::isblank(c);
    }

    void AcceptEncoding::parseRaw(const char* const str, const size_t len)
    {
        const char* const str_end = str + len;
        const char* start         = str;

        while (start != str_end)
        {
            // Per RFC 9110, if no "q" parameter is present, the default weight is 1
            float qvalue = 1;

            const char* const end = std::find(start, str_end, ',');

            const char* const token_end = std::find_if_not(start, end, is_http_token);

            // If no semicolon is found, it means that no q-value is present
            const char* const semicolon = std::find(token_end, end, ';');
            if (semicolon != end)
            {
                // Skip optional white space
                const char* ows_end = std::find_if_not(semicolon + std::strlen(";"), end, is_http_space);

                if (ows_end[0] != 'q' || ows_end[1] != '=')
                {
                    // "q=" is expected after the optional white space. If there
                    // isn't, this is a malformed header
                    encodings_.clear();
                    return;
                }

                const char* const value_str = ows_end + std::strlen("q=");

                std::size_t qvalue_len;
                const bool valid = strToQvalue(value_str, &qvalue, &qvalue_len);
                if (!valid)
                {
                    encodings_.clear();
                    return;
                }
            }

            const std::string_view encodingStr(start, token_end - start);
            if (!encodingStr.empty())
            {
                insertEncoding(std::make_pair(
                    encodingFromString(std::string_view(start, token_end - start)),
                    qvalue));
            }

            // Go to the next token for the next iteration
            start = std::find_if(end, str_end, is_http_token);
        }
    }

    void AcceptEncoding::write(std::ostream& os) const
    {
        if (encodings_.empty())
        {
            return;
        }

        for (size_t i = 0; i < encodings_.size() - 1; i++)
        {
            os << encodingString(encodings_[i].first) << ";q=" << encodings_[i].second << ", ";
        }
        os << encodingString(encodings_[encodings_.size() - 1].first) << ";q=" << encodings_[encodings_.size() - 1].second;
    }

    const std::vector<std::pair<Encoding, float>>& AcceptEncoding::encodings() const
    {
        return encodings_;
    }

    void AcceptEncoding::insertEncoding(const std::pair<Encoding, float>& elem)
    {
        encodings_.insert(
            std::upper_bound(
                encodings_.cbegin(), encodings_.cend(),
                elem,
                [](decltype(elem) a, decltype(elem) b) {
                    return a.second > b.second;
                }),
            elem);
    }

    void EncodingHeader::write(std::ostream& os) const
    {
        os << encodingString(encoding_);
    }

    Server::Server(const std::vector<std::string>& tokens)
        : tokens_(tokens)
    { }

    Server::Server(const std::string& token)
        : tokens_()
    {
        tokens_.push_back(token);
    }

    Server::Server(const char* token)
        : tokens_()
    {
        tokens_.emplace_back(token);
    }

    void Server::parse(const std::string& token) { tokens_.push_back(token); }

    void Server::write(std::ostream& os) const
    {
        for (size_t i = 0; i < tokens_.size(); i++)
        {
            const auto& token = tokens_[i];
            os << token;
            if (i < tokens_.size() - 1)
            {
                os << " ";
            }
        }
    }

    void ContentType::parseRaw(const char* str, size_t len)
    {
        mime_.parseRaw(str, len);
    }

    void ContentType::write(std::ostream& os) const { os << mime_.toString(); }

} // namespace Pistache::Http::Header
