#include <pistache/http_headers.h>
#include <pistache/date.h>

#include "gtest/gtest.h"

#include <algorithm>
#include <chrono>
#include <iostream>

using namespace Pistache::Http;

TEST(headers_test, accept) {
    Pistache::Http::Header::Accept a1;
    a1.parse("audio/*; q=0.2");

    {
        const auto& media = a1.media();
        ASSERT_EQ(media.size(), 1U);

        const auto& mime = media[0];
        ASSERT_EQ(mime, MIME(Audio, Star));
        ASSERT_EQ(mime.q().getOrElse(Mime::Q(0)), Mime::Q(20));
    }

    Pistache::Http::Header::Accept a2;
    a2.parse("text/*, text/html, text/html;level=1, */*");

    {
        const auto& media = a2.media();
        ASSERT_EQ(media.size(), 4U);

        const auto &m1 = media[0];
        ASSERT_EQ(m1, MIME(Text, Star));
        const auto &m2 = media[1];
        ASSERT_EQ(m2, MIME(Text, Html));
        const auto& m3 = media[2];
        ASSERT_EQ(m3, MIME(Text, Html));
        auto level = m3.getParam("level");
        ASSERT_EQ(level.getOrElse(""), "1");
        const auto& m4 = media[3];
        ASSERT_EQ(m4, MIME(Star, Star));
    }

    Pistache::Http::Header::Accept a3;
    a3.parse("text/*;q=0.3, text/html;q=0.7, text/html;level=1, "
             "text/html;level=2;q=0.4, */*;q=0.5");

    {
        const auto& media = a3.media();
        ASSERT_EQ(media.size(), 5U);

        ASSERT_EQ(media[0], MIME(Text, Star));
        ASSERT_EQ(media[0].q().getOrElse(Mime::Q(0)), Mime::Q(30));

        ASSERT_EQ(media[1], MIME(Text, Html));
        ASSERT_EQ(media[2], MIME(Text, Html));
        ASSERT_EQ(media[3], MIME(Text, Html));
        ASSERT_EQ(media[4], MIME(Star, Star));
        ASSERT_EQ(media[4].q().getOrElse(Mime::Q(0)), Mime::Q::fromFloat(0.5));
    }

    Pistache::Http::Header::Accept a4;
    ASSERT_THROW(a4.parse("text/*;q=0.4, text/html;q=0.3,"), std::runtime_error);

    Pistache::Http::Header::Accept a5;
    ASSERT_THROW(a5.parse("text/*;q=0.4, text/html;q=0.3, "), std::runtime_error);
}

TEST(headers_test, allow) {
    Pistache::Http::Header::Allow a1(Method::Get);

    std::ostringstream os;
    a1.write(os);
    ASSERT_EQ(os.str(), "GET");
    os.str("");

    Pistache::Http::Header::Allow a2({ Method::Post, Method::Put });
    a2.write(os);
    ASSERT_EQ(os.str(), "POST, PUT");
    os.str("");

    Pistache::Http::Header::Allow a3;
    a3.addMethod(Method::Get);
    a3.write(os);
    ASSERT_EQ(os.str(), "GET");
    os.str("");
    a3.addMethod(Method::Options);
    a3.write(os);
    ASSERT_EQ(os.str(), "GET, OPTIONS");
    os.str("");

    Pistache::Http::Header::Allow a4(Method::Head);
    a4.addMethods({ Method::Get, Method::Options });
    a4.write(os);
    ASSERT_EQ(os.str(), "HEAD, GET, OPTIONS");
    os.str("");

    Pistache::Http::Header::Allow a5(Method::Head);
    std::vector<Method> methods;
    methods.push_back(Method::Get);
    a5.addMethods(methods);
    a5.write(os);
    ASSERT_EQ(os.str(), "HEAD, GET");
}

TEST(headers_test, cache_control) {
    auto testTrivial = [](std::string str, CacheDirective::Directive expected) {
        Pistache::Http::Header::CacheControl cc;
        cc.parse(str);

        auto directives = cc.directives();
        ASSERT_EQ(directives.size(), 1U);
        ASSERT_EQ(directives[0].directive(), expected);
    };

    auto testTimed = [](
            std::string str, CacheDirective::Directive expected, uint64_t delta) {
        Pistache::Http::Header::CacheControl cc;
        cc.parse(str);

        auto directives = cc.directives();
        ASSERT_EQ(directives.size(), 1U);

        ASSERT_EQ(directives[0].directive(), expected);
        ASSERT_EQ(directives[0].delta(), std::chrono::seconds(delta));
    };

    testTrivial("no-cache", CacheDirective::NoCache);
    testTrivial("no-store", CacheDirective::NoStore);
    testTrivial("no-transform", CacheDirective::NoTransform);
    testTrivial("only-if-cached", CacheDirective::OnlyIfCached);

    testTimed("max-age=0", CacheDirective::MaxAge, 0);
    testTimed("max-age=12", CacheDirective::MaxAge, 12);

    testTimed("max-stale=12345", CacheDirective::MaxStale, 12345);
    testTimed("min-fresh=48", CacheDirective::MinFresh, 48);

    Pistache::Http::Header::CacheControl cc1;
    cc1.parse("private, max-age=600");
    auto d1 = cc1.directives();
    ASSERT_EQ(d1.size(), 2U);
    ASSERT_EQ(d1[0].directive(), CacheDirective::Private);
    ASSERT_EQ(d1[1].directive(), CacheDirective::MaxAge);
    ASSERT_EQ(d1[1].delta(), std::chrono::seconds(600));

    Pistache::Http::Header::CacheControl cc2;
    cc2.parse("public, s-maxage=200, proxy-revalidate");
    auto d2 = cc2.directives();
    ASSERT_EQ(d2.size(), 3U);
    ASSERT_EQ(d2[0].directive(), CacheDirective::Public);
    ASSERT_EQ(d2[1].directive(), CacheDirective::SMaxAge);
    ASSERT_EQ(d2[1].delta(), std::chrono::seconds(200));
    ASSERT_EQ(d2[2].directive(), CacheDirective::ProxyRevalidate);

    Pistache::Http::Header::CacheControl cc3(CacheDirective::NoCache);
    std::ostringstream oss;
    cc3.write(oss);
    ASSERT_EQ(oss.str(), "no-cache");
    oss.str("");

    cc3.addDirective(CacheDirective::NoStore);
    cc3.write(oss);
    ASSERT_EQ(oss.str(), "no-cache, no-store");
    oss.str("");

    Pistache::Http::Header::CacheControl cc4(CacheDirective::NoTransform);
    cc4.write(oss);
    ASSERT_EQ(oss.str(), "no-transform");
    oss.str("");

    Pistache::Http::Header::CacheControl cc5(CacheDirective::OnlyIfCached);
    cc5.write(oss);
    ASSERT_EQ(oss.str(), "only-if-cached");
    oss.str("");

    Pistache::Http::Header::CacheControl cc6(CacheDirective::Private);
    cc6.write(oss);
    ASSERT_EQ(oss.str(), "private");
    oss.str("");

    Pistache::Http::Header::CacheControl cc7(CacheDirective::Public);
    cc7.write(oss);
    ASSERT_EQ(oss.str(), "public");
    oss.str("");

    Pistache::Http::Header::CacheControl cc8(CacheDirective::MustRevalidate);
    cc8.write(oss);
    ASSERT_EQ(oss.str(), "must-revalidate");
    oss.str("");

    Pistache::Http::Header::CacheControl cc9(CacheDirective::ProxyRevalidate);
    cc9.write(oss);
    ASSERT_EQ(oss.str(), "proxy-revalidate");
    oss.str("");

    Pistache::Http::Header::CacheControl cc10(CacheDirective(CacheDirective::MaxStale, std::chrono::seconds(12345)));
    cc10.write(oss);
    ASSERT_EQ(oss.str(), "max-stale=12345");
    oss.str("");

    Pistache::Http::Header::CacheControl cc11(CacheDirective(CacheDirective::MinFresh, std::chrono::seconds(12345)));
    cc11.write(oss);
    ASSERT_EQ(oss.str(), "min-fresh=12345");
    oss.str("");

    Pistache::Http::Header::CacheControl cc14(CacheDirective(CacheDirective::SMaxAge, std::chrono::seconds(12345)));
    cc14.write(oss);
    ASSERT_EQ(oss.str(), "s-maxage=12345");
    oss.str("");

    Pistache::Http::Header::CacheControl cc15(CacheDirective::Ext);
    cc15.write(oss);
    ASSERT_EQ(oss.str(), "");
    oss.str("");

    Pistache::Http::Header::CacheControl cc16;
    cc16.write(oss);
    ASSERT_EQ(oss.str(), "");
    oss.str("");

    Pistache::Http::Header::CacheControl cc12;
    cc12.addDirectives({ CacheDirective::Public, CacheDirective(CacheDirective::MaxAge, std::chrono::seconds(600)) });
    cc12.write(oss);
    ASSERT_EQ(oss.str(), "public, max-age=600");
    oss.str("");

    Pistache::Http::Header::CacheControl cc13;
    std::vector<Pistache::Http::CacheDirective> cd;

    cd.push_back(CacheDirective::Public);
    cd.push_back(CacheDirective(CacheDirective::MaxAge, std::chrono::seconds(600)));

    cc13.addDirectives(cd);
    cc13.write(oss);
    ASSERT_EQ(oss.str(), "public, max-age=600");

}

TEST(headers_test, content_length) {
    Pistache::Http::Header::ContentLength cl;
    std::ostringstream oss;
    cl.parse("3495");
    cl.write(oss);

    ASSERT_EQ("3495", oss.str());
    ASSERT_EQ(cl.value(), 3495U);
}

TEST(headers_test, expect_test)
{
    Pistache::Http::Header::Expect e;
    std::ostringstream oss;
    e.parse("100-continue");
    e.write(oss);

    ASSERT_EQ("100-continue", oss.str());
    ASSERT_EQ(e.expectation(), Pistache::Http::Expectation::Continue);
    oss.str("");

    e.parse("unknown");
    e.write(oss);

    ASSERT_EQ("", oss.str());
    ASSERT_EQ(e.expectation(), Pistache::Http::Expectation::Ext);
    oss.str("");

}

TEST(headers_test, connection) {
    Pistache::Http::Header::Connection connection;

    constexpr struct Test {
        const char *data;
        ConnectionControl expected;
        const char *expected_string;
    } tests[] = {
      { "close", ConnectionControl::Close, "Close" },
      { "clOse", ConnectionControl::Close, "Close" },
      { "Close", ConnectionControl::Close, "Close" },
      { "CLOSE", ConnectionControl::Close, "Close" },

      { "keep-alive", ConnectionControl::KeepAlive, "Keep-Alive" },
      { "Keep-Alive", ConnectionControl::KeepAlive, "Keep-Alive" },
      { "kEEp-alIvE", ConnectionControl::KeepAlive, "Keep-Alive" },
      { "KEEP-ALIVE", ConnectionControl::KeepAlive, "Keep-Alive" },

    { "Ext", ConnectionControl::Ext, "Ext" },
    { "ext", ConnectionControl::Ext, "Ext" },
    { "eXt", ConnectionControl::Ext, "Ext" },
    { "eXT", ConnectionControl::Ext, "Ext" }
    };

    for (auto test: tests) {
        Pistache::Http::Header::Connection connection;
        std::ostringstream oss;
        connection.parse(test.data);
        connection.write(oss);

        ASSERT_EQ(connection.control(), test.expected);
        ASSERT_EQ(oss.str(), test.expected_string);
    }
}


TEST(headers_test, date_test_rfc_1123) {

    using namespace std::chrono;
    FullDate::time_point expected_time_point = date::sys_days(date::year{1994}/11/6)
                                                + hours(8) + minutes(49) + seconds(37);

    /* RFC-1123 */
    Pistache::Http::Header::Date d1;
    d1.parse("Sun, 06 Nov 1994 08:49:37 GMT");
    auto dd1 = d1.fullDate().date();
    ASSERT_EQ(expected_time_point, dd1);
}

TEST(headers_test, date_test_rfc_850) {

    using namespace std::chrono;
    FullDate::time_point expected_time_point = date::sys_days(date::year{1994}/11/6)
                                                + hours(8) + minutes(49) + seconds(37);

    /* RFC-850 */
    Pistache::Http::Header::Date d2;
    d2.parse("Sunday, 06-Nov-94 08:49:37 GMT");
    auto dd2 = d2.fullDate().date();
    ASSERT_EQ(dd2, expected_time_point);
}

TEST(headers_test, date_test_asctime) {

    using namespace std::chrono;
    FullDate::time_point expected_time_point = date::sys_days(date::year{1994}/11/6)
                                                + hours(8) + minutes(49) + seconds(37);

    /* ANSI C's asctime format */
    Pistache::Http::Header::Date d3;
    d3.parse("Sun Nov  6 08:49:37 1994");
    auto dd3 = d3.fullDate().date();
    ASSERT_EQ(dd3, expected_time_point);
}

TEST(headers_test, date_test_ostream) {

    std::ostringstream os;

    Pistache::Http::Header::Date d4;
    d4.parse("Fri, 25 Jan 2019 21:04:45.000000000 UTC");
    d4.write(os);
    ASSERT_EQ("Fri, 25 Jan 2019 21:04:45.000000000 UTC", os.str());
}

TEST(headers_test, host) {


    Pistache::Http::Header::Host host("www.w3.org");
    std::ostringstream oss;
    host.write(oss);

    ASSERT_EQ(host.host(), "www.w3.org");
    ASSERT_EQ(host.port(), 80);
    ASSERT_EQ(oss.str(), "www.w3.org:80");
    oss.str("");

    host.parse("www.example.com:8080");
    host.write(oss);

    ASSERT_EQ(host.host(), "www.example.com");
    ASSERT_EQ(host.port(), 8080);
    ASSERT_EQ(oss.str(), "www.example.com:8080");
    oss.str("");

    host.parse("localhost:8080");
    host.write(oss);

    ASSERT_EQ(host.host(), "localhost");
    ASSERT_EQ(host.port(), 8080);
    ASSERT_EQ(oss.str(), "localhost:8080");
    oss.str("");

/* Due to an error in GLIBC these tests don't fail as expected, further research needed */
//     ASSERT_THROW( host.parse("256.256.256.256:8080");, std::invalid_argument);
//     ASSERT_THROW( host.parse("1.0.0.256:8080");, std::invalid_argument);

    host.parse("[::1]:8080");
    host.write(oss);

    ASSERT_EQ(host.host(), "[::1]");
    ASSERT_EQ(host.port(), 8080);
    ASSERT_EQ(oss.str(), "[::1]:8080");
    oss.str("");

    host.parse("[2001:0DB8:AABB:CCDD:EEFF:0011:2233:4455]:8080");
    host.write(oss);

    ASSERT_EQ(host.host(), "[2001:0DB8:AABB:CCDD:EEFF:0011:2233:4455]");
    ASSERT_EQ(host.port(), 8080);
    ASSERT_EQ(oss.str(), "[2001:0DB8:AABB:CCDD:EEFF:0011:2233:4455]:8080");
    oss.str("");

/* Due to an error in GLIBC these tests don't fail as expected, further research needed */
//     ASSERT_THROW( host.parse("[GGGG:GGGG:GGGG:GGGG:GGGG:GGGG:GGGG:GGGG]:8080");, std::invalid_argument);
//     ASSERT_THROW( host.parse("[::GGGG]:8080");, std::invalid_argument);
}

TEST(headers_test, user_agent) {
    Pistache::Http::Header::UserAgent ua;
    std::ostringstream os;

    ua.parse("CERN-LineMode/2.15 libwww/2.17b3");
    ua.write(os);

    ASSERT_TRUE(std::strcmp(os.str().c_str(), "CERN-LineMode/2.15 libwww/2.17b3") == 0);
    ASSERT_EQ(ua.agent(), "CERN-LineMode/2.15 libwww/2.17b3");
}

TEST(headers_test, content_encoding) {
    Pistache::Http::Header::ContentEncoding ce;
    std::ostringstream oss;
    ce.parse("gzip");
    ce.write(oss);
    ASSERT_EQ("gzip", oss.str());
    ASSERT_EQ(ce.encoding(), Pistache::Http::Header::Encoding::Gzip);
    oss.str("");

    ce.parse("deflate");
    ce.write(oss);
    ASSERT_EQ("deflate", oss.str());
    ASSERT_EQ(ce.encoding(), Pistache::Http::Header::Encoding::Deflate);
    oss.str("");

    ce.parse("compress");
    ce.write(oss);
    ASSERT_EQ("compress", oss.str());
    ASSERT_EQ(ce.encoding(), Pistache::Http::Header::Encoding::Compress);
    oss.str("");

    ce.parse("identity");
    ce.write(oss);
    ASSERT_EQ("identity", oss.str());
    ASSERT_EQ(ce.encoding(), Pistache::Http::Header::Encoding::Identity);
    oss.str("");

    ce.parse("chunked");
    ce.write(oss);
    ASSERT_EQ("chunked", oss.str());
    ASSERT_EQ(ce.encoding(), Pistache::Http::Header::Encoding::Chunked);
    oss.str("");

    ce.parse("unknown");
    ce.write(oss);
    ASSERT_EQ("unknown", oss.str());
    ASSERT_EQ(ce.encoding(), Pistache::Http::Header::Encoding::Unknown);
    oss.str("");
}



TEST(headers_test, content_type) {
    Pistache::Http::Header::ContentType ct;
    std::ostringstream oss;
    ct.parse("text/html; charset=ISO-8859-4");
    ct.write(oss);

    ASSERT_EQ("text/html; charset=ISO-8859-4", oss.str());
    const auto& mime = ct.mime();
    ASSERT_EQ(mime, MIME(Text, Html));
    ASSERT_EQ(mime.getParam("charset").getOrElse(""), "ISO-8859-4");
}

TEST(headers_test, access_control_allow_origin_test)
{
    Pistache::Http::Header::AccessControlAllowOrigin allowOrigin;
    std::ostringstream os;

    allowOrigin.parse("http://foo.bar");
    allowOrigin.write(os);

    ASSERT_TRUE(std::strcmp(os.str().c_str(), "http://foo.bar") == 0);
    ASSERT_EQ(allowOrigin.uri(), "http://foo.bar");
}

TEST(headers_test, access_control_allow_headers_test)
{
    Pistache::Http::Header::AccessControlAllowHeaders allowHeaders;
    std::ostringstream os;

    allowHeaders.parse("Content-Type, Access-Control-Allow-Headers, Authorization, X-Requested-With");
    allowHeaders.write(os);

    ASSERT_TRUE(std::strcmp(os.str().c_str(), "Content-Type, Access-Control-Allow-Headers, Authorization, X-Requested-With") == 0);
    ASSERT_EQ(allowHeaders.val(), "Content-Type, Access-Control-Allow-Headers, Authorization, X-Requested-With");
}

TEST(headers_test, access_control_expose_headers_test)
{
    Pistache::Http::Header::AccessControlExposeHeaders exposeHeaders;
    std::ostringstream os;

    exposeHeaders.parse("Accept, Location");
    exposeHeaders.write(os);

    ASSERT_EQ(exposeHeaders.val(), "Accept, Location");
    ASSERT_TRUE(std::strcmp(os.str().c_str(), "Accept, Location") == 0);
}

TEST(headers_test, access_control_allow_methods_test)
{
    Pistache::Http::Header::AccessControlAllowMethods allowMethods;
    std::ostringstream os;

    allowMethods.parse("GET, POST, DELETE");
    allowMethods.write(os);

    ASSERT_EQ(allowMethods.val(), "GET, POST, DELETE");
    ASSERT_TRUE(std::strcmp(os.str().c_str(), "GET, POST, DELETE") == 0);

}

TEST(headers_test, location_test)
{

    Pistache::Http::Header::Location l0("location");
    std::ostringstream oss;
    l0.write(oss);
    ASSERT_EQ("location", oss.str());
    oss.str("");

    Pistache::Http::Header::Location l1;
    l1.parse("location");
    l1.write(oss);
    ASSERT_EQ("location", oss.str());
    oss.str("");

}

TEST(headers_test, server_test)
{

    Pistache::Http::Header::Server s0("server");
    std::ostringstream oss;
    s0.write(oss);
    ASSERT_EQ("server", oss.str());
    oss.str("");

    std::vector<std::string> tokens({"server0", "server1"});
    Pistache::Http::Header::Server s1(tokens);
    s1.write(oss);
    ASSERT_EQ("server0 server1", oss.str());
    oss.str("");

    std::string token("server");
    Pistache::Http::Header::Server s2(token);
    s2.write(oss);
    ASSERT_EQ("server", oss.str());
    oss.str("");

    Pistache::Http::Header::Server s3;
    s3.parse("server");
    s3.write(oss);
    ASSERT_EQ("server", oss.str());
    oss.str("");

}

CUSTOM_HEADER(TestHeader)

TEST(headers_test, macro_for_custom_headers)
{
    TestHeader testHeader;
    std::ostringstream os;

    ASSERT_TRUE( strcmp(TestHeader::Name,"TestHeader") == 0);

    testHeader.parse("Header Content Test");
    testHeader.write(os);

    ASSERT_EQ(testHeader.val(), "Header Content Test");
    ASSERT_TRUE( std::strcmp(os.str().c_str(), "Header Content Test") == 0);
}

TEST(headers_test, add_new_header_test)
{
    const std::string headerName = "TestHeader";

    ASSERT_FALSE(Pistache::Http::Header::Registry::instance().isRegistered(headerName));
    Pistache::Http::Header::Registry::instance().registerHeader<TestHeader>();
    ASSERT_TRUE(Pistache::Http::Header::Registry::instance().isRegistered(headerName));

    const auto& headersList = Pistache::Http::Header::Registry::instance().headersList();
    const bool isFound = std::find(headersList.begin(), headersList.end(), headerName) != headersList.end();
    ASSERT_TRUE(isFound);
}



//throw std::runtime_error("Header already registered");
//throw std::runtime_error("Unknown header");
//throw std::runtime_error("Could not find header");
//    Collection::get(const std::string& name) const
//    Collection::get(const std::string& name)
//    Collection::getRaw(const std::string& name) const

using namespace Pistache::Http::Header;

TEST(headers_test, header_already_registered)
{
    std::string what;

    try {
        RegisterHeader(Accept);
    } catch (std::exception& e) {
        what = e.what();
    }

    ASSERT_EQ("Header already registered", what);

}

TEST(headers_test, unknown_header)
{

    std::string what;

    try {
        auto h = Pistache::Http::Header::Registry::instance().makeHeader("UnknownHeader");
    } catch (std::exception& e) {
        what = e.what();
    }

    ASSERT_EQ("Unknown header", what);

}

TEST(headers_test, could_not_find_header)
{

    std::string what;

    try {
        auto h = Pistache::Http::Header::Registry::instance().makeHeader("UnknownHeader");
    } catch (std::exception& e) {
        what = e.what();
    }

    ASSERT_EQ("Unknown header", what);

}