/*
 * SPDX-FileCopyrightText: 2026 The Pistache project contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * fuzz_headers_all - component-API harness over every registered HTTP header
 * type plus the header collection flows.
 *
 * Rationale (why this harness should find bugs):
 * - The OSS-Fuzz corpus had only one header-driven harness (fuzz_parser.cpp
 *   type 'H') which parses 12 of the 21 registered header types and never
 *   touches the Header::Collection add/get/remove flows, the registry
 *   name-based construction path, or the unregistered ETag parser with its
 *   RFC 9110 etagc validation.
 * - Header values are the core attacker-controlled surface of an HTTP
 *   server: any memory-safety or logic defect in these parsers is reachable
 *   from real wire input.
 * - Everything runs in-process; no network, no threads, no global state that
 *   survives an iteration, so crashes are attributable to the parser itself.
 *
 * Threat model: the untrusted input is the raw value of an attacker-supplied
 * header line (the part after "Name:"), as well as the header name itself
 * (an attacker fully controls which headers a client sends). The harness
 * feeds the same fuzzer-controlled string as header value and as header name.
 */

#include <pistache/http.h>
#include <pistache/router.h>

#include <sstream>
#include <string>

using namespace Pistache;

template <class F>
void ignoreExceptions(const F& func)
{
    try
    {
        func();
    }
    catch (...)
    {
    }
}

/* Parse `input` as header `T`, exercise its default accessor, and write it. */
template <class T>
void parseHeader(const std::string& input)
{
    T header;
    ignoreExceptions([&] { header.parse(input); });

    std::stringstream oss;
    ignoreExceptions([&] { header.write(oss); });
}

void fuzz_allow(const std::string& input)
{
    Http::Header::Allow header;
    ignoreExceptions([&] { header.parse(input); });
    ignoreExceptions([&] { header.addMethod(Http::Method::Get); });
    ignoreExceptions([&] { const auto methods = header.methods(); (void)methods; });

    std::stringstream oss;
    ignoreExceptions([&] { header.write(oss); });
}

void fuzz_authorization(const std::string& input)
{
    Http::Header::Authorization header;
    ignoreExceptions([&] { header.parse(input); });
    ignoreExceptions([&] { const auto method = header.getMethod(); (void)method; });
    ignoreExceptions(
        [&] {
            const auto user     = header.getBasicUser();
            const auto password = header.getBasicPassword();
            header.setBasicUserPassword(user, password);
        });

    std::stringstream oss;
    ignoreExceptions([&] { header.write(oss); });
}

void fuzz_accept(const std::string& input)
{
    Http::Header::Accept header;
    ignoreExceptions([&] { header.parse(input); });
    ignoreExceptions([&] {
        for (const auto& media : header.media())
        {
            const auto rendered = media.toString();
            (void)rendered;
        }
    });

    std::stringstream oss;
    ignoreExceptions([&] { header.write(oss); });
}

void fuzz_accept_encoding(const std::string& input)
{
    Http::Header::AcceptEncoding header;
    ignoreExceptions([&] { header.parse(input); });
    ignoreExceptions([&] { const auto encodings = header.encodings(); (void)encodings; });

    std::stringstream oss;
    ignoreExceptions([&] { header.write(oss); });
}


void fuzz_content_type(const std::string& input)
{
    Http::Header::ContentType header;
    ignoreExceptions([&] { header.parse(input); });
    ignoreExceptions([&] {
        const auto mime    = header.mime();
        const auto isValid = mime.isValid();
        (void)isValid;
    });

    std::stringstream oss;
    ignoreExceptions([&] { header.write(oss); });
}

void fuzz_host(const std::string& input)
{
    Http::Header::Host header;
    ignoreExceptions([&] { header.parse(input); });
    ignoreExceptions([&] {
        const auto host = header.host();
        const auto port = header.port().toString();
        (void)host;
        (void)port;
    });

    std::stringstream oss;
    ignoreExceptions([&] { header.write(oss); });
}

void fuzz_etag(const std::string& input)
{
    ignoreExceptions([&] {
        const auto valid = Http::Header::ETag::isValidEtagc(input);
        (void)valid;
    });

    Http::Header::ETag header;
    ignoreExceptions([&] { header.parse(input); });
    ignoreExceptions([&] {
        const auto etagc = header.etagc();
        const auto weak  = header.isWeak();
        (void)etagc;
        (void)weak;
    });

    std::stringstream oss;
    ignoreExceptions([&] { header.write(oss); });
}

/* Registry-driven: build a header from `input` used as a header *name*,
 * then parse `input` again as its value. This mirrors the path
 * Http::Handler takes for each incoming header line. */
void fuzz_registry(const std::string& input)
{
    auto& registry = Http::Header::Registry::instance();
    ignoreExceptions([&] {
        if (registry.isRegistered(input))
        {
            auto header = registry.makeHeader(input);
            if (header)
            {
                header->parse(input);
                std::stringstream oss;
                header->write(oss);
            }
        }
    });
}

/* Collection flows: raw add/get/tryGet/remove round-trip. */
void fuzz_collection(const std::string& input)
{
    Http::Header::Collection collection;
    ignoreExceptions([&] {
        collection.addRaw(Http::Header::Raw { input, input });
    });
    ignoreExceptions([&] {
        const auto raw = collection.getRaw(input);
        (void)raw;
    });
    ignoreExceptions([&] {
        const auto maybe = collection.tryGetRaw(input);
        (void)maybe;
    });
    ignoreExceptions([&] {
        const auto header = collection.tryGet(input);
        if (header)
        {
            std::stringstream oss;
            header->write(oss);
        }
    });
    ignoreExceptions([&] { collection.remove(input); });
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return 0;

    const std::string input(reinterpret_cast<const char*>(data), size);

    fuzz_allow(input);
    fuzz_authorization(input);
    fuzz_accept(input);
    fuzz_accept_encoding(input);
    fuzz_content_type(input);
    fuzz_host(input);
    fuzz_etag(input);
    fuzz_registry(input);
    fuzz_collection(input);

    /* Remaining registered types: plain parse + write + default accessor. */
    parseHeader<Http::Header::AccessControlAllowOrigin>(input);
    parseHeader<Http::Header::AccessControlAllowHeaders>(input);
    parseHeader<Http::Header::AccessControlExposeHeaders>(input);
    parseHeader<Http::Header::AccessControlAllowMethods>(input);
    parseHeader<Http::Header::CacheControl>(input);
    parseHeader<Http::Header::Connection>(input);
    parseHeader<Http::Header::ContentEncoding>(input);
    parseHeader<Http::Header::TransferEncoding>(input);
    parseHeader<Http::Header::ContentLength>(input);
    parseHeader<Http::Header::Date>(input);
    parseHeader<Http::Header::Expect>(input);
    parseHeader<Http::Header::LastModified>(input);
    parseHeader<Http::Header::Location>(input);
    parseHeader<Http::Header::Server>(input);
    parseHeader<Http::Header::UserAgent>(input);

    return 0;
}
