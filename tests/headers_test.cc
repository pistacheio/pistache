/*
 * SPDX-FileCopyrightText: 2015 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <date/date.h>
#include <pistache/http.h>

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>

using testing::ElementsAre;
using testing::SizeIs;
using testing::UnorderedElementsAre;

TEST(headers_test, accept)
{
    Pistache::Http::Header::Accept a1;
    a1.parse("audio/*; q=0.2");

    {
        const auto& media = a1.media();
        ASSERT_EQ(media.size(), 1U);

        const auto& mime = media[0];
        ASSERT_EQ(mime, MIME(Audio, Star));
        ASSERT_EQ(mime.q().value_or(Pistache::Http::Mime::Q(0)), Pistache::Http::Mime::Q(20));

        std::ostringstream oss;
        a1.write(oss);
        ASSERT_EQ(oss.str(), "audio/*; q=0.2");
    }

    Pistache::Http::Header::Accept a2;
    a2.parse("text/*, text/html, text/html;level=1, */*");

    {
        const auto& media = a2.media();
        ASSERT_EQ(media.size(), 4U);

        const auto& m1 = media[0];
        ASSERT_EQ(m1, MIME(Text, Star));
        const auto& m2 = media[1];
        ASSERT_EQ(m2, MIME(Text, Html));
        const auto& m3 = media[2];
        ASSERT_EQ(m3, MIME(Text, Html));
        auto level = m3.getParam("level");
        ASSERT_EQ(level.value_or(""), "1");
        const auto& m4 = media[3];
        ASSERT_EQ(m4, MIME(Star, Star));

        std::ostringstream oss;
        a2.write(oss);
        ASSERT_EQ(oss.str(), "text/*, text/html, text/html;level=1, */*");
    }

    Pistache::Http::Header::Accept a3;
    a3.parse("text/*;q=0.3, text/html;q=0.7, text/html;level=1, "
             "text/html;level=2;q=0.4, */*;q=0.5");

    {
        const auto& media = a3.media();
        ASSERT_EQ(media.size(), 5U);

        ASSERT_EQ(media[0], MIME(Text, Star));
        ASSERT_EQ(media[0].q().value_or(Pistache::Http::Mime::Q(0)), Pistache::Http::Mime::Q(30));

        ASSERT_EQ(media[1], MIME(Text, Html));
        ASSERT_EQ(media[2], MIME(Text, Html));
        ASSERT_EQ(media[3], MIME(Text, Html));
        ASSERT_EQ(media[4], MIME(Star, Star));
        ASSERT_EQ(media[4].q().value_or(Pistache::Http::Mime::Q(0)), Pistache::Http::Mime::Q::fromFloat(0.5));

        std::ostringstream oss;
        a3.write(oss);
        ASSERT_EQ(oss.str(), "text/*;q=0.3, text/html;q=0.7, text/html;level=1, text/html;level=2;q=0.4, */*;q=0.5");
    }

    Pistache::Http::Header::Accept a4;
    ASSERT_THROW(a4.parse("text/*;q=0.4, text/html;q=0.3,"), std::runtime_error);
    /* Shameless dummy comment to work around syntax highlighting bug in nano...
     */

    Pistache::Http::Header::Accept a5;
    ASSERT_THROW(a5.parse("text/*;q=0.4, text/html;q=0.3, "), std::runtime_error);
    /* Shameless dummy comment to work around syntax highlighting bug in nano...
     */
}

TEST(headers_test, accept_encoding)
{
    using Pistache::Http::Header::AcceptEncoding;
    using Pistache::Http::Header::Encoding;

    AcceptEncoding a1;
    a1.parse("compress, gzip");
    EXPECT_THAT(
        a1.encodings(),
        UnorderedElementsAre(
            std::make_pair(Encoding::Compress, 1.0F),
            std::make_pair(Encoding::Gzip, 1.0F)));

    AcceptEncoding a2;
    a2.parse("");
    EXPECT_THAT(
        a2.encodings(),
        ElementsAre());

    AcceptEncoding a3;
    a3.parse("compress;q=0.5, gzip ; q=1.0");
    EXPECT_THAT(
        a3.encodings(),
        ElementsAre(
            std::make_pair(Encoding::Gzip, 1.0F),
            std::make_pair(Encoding::Compress, 0.5F)));

    AcceptEncoding a4;
    a4.parse("gzip;q=1.0, identity; q=0.5, *;q=0");
    EXPECT_THAT(
        a4.encodings(),
        ElementsAre(
            std::make_pair(Encoding::Gzip, 1.0F),
            std::make_pair(Encoding::Identity, 0.5F),
            std::make_pair(Encoding::Unknown, 0.0F)));

    AcceptEncoding a5;
    a5.parse("gzip;q=1.0, identity; q=0.5, br;q=0.7, *;q=0");
    EXPECT_THAT(
        a5.encodings(),
        ElementsAre(
            std::make_pair(Encoding::Gzip, 1.0F),
            std::make_pair(Encoding::Br, 0.7F),
            std::make_pair(Encoding::Identity, 0.5F),
            std::make_pair(Encoding::Unknown, 0.0F)));

    AcceptEncoding a6;
    a6.parse("br;");
    EXPECT_THAT(
        a6.encodings(),
        SizeIs(0));

    AcceptEncoding a7;
    a7.parse("br;q=");
    EXPECT_THAT(
        a7.encodings(),
        SizeIs(0));

    AcceptEncoding a8;
    a8.parse("deflate;");
    EXPECT_THAT(
        a8.encodings(),
        SizeIs(0));

    AcceptEncoding a9;
    a9.parse("deflate;q=");
    EXPECT_THAT(
        a9.encodings(),
        SizeIs(0));

    AcceptEncoding a10;
    a10.parse(",");
    EXPECT_THAT(
        a10.encodings(),
        SizeIs(0));

    AcceptEncoding a11;
    a11.parse("deflate;a=1");
    EXPECT_THAT(
        a11.encodings(),
        SizeIs(0));
}

TEST(headers_test, allow)
{
    Pistache::Http::Header::Allow a1(Pistache::Http::Method::Get);

    std::ostringstream os;
    a1.write(os);
    ASSERT_EQ(os.str(), "GET");
    os.str("");

    Pistache::Http::Header::Allow a2(
        { Pistache::Http::Method::Post, Pistache::Http::Method::Put });
    a2.write(os);
    ASSERT_EQ(os.str(), "POST, PUT");
    os.str("");

    Pistache::Http::Header::Allow a3;
    a3.addMethod(Pistache::Http::Method::Get);
    a3.write(os);
    ASSERT_EQ(os.str(), "GET");
    os.str("");
    a3.addMethod(Pistache::Http::Method::Options);
    a3.write(os);
    ASSERT_EQ(os.str(), "GET, OPTIONS");
    os.str("");

    Pistache::Http::Header::Allow a4(Pistache::Http::Method::Head);
    a4.addMethods({ Pistache::Http::Method::Get, Pistache::Http::Method::Options });
    a4.write(os);
    ASSERT_EQ(os.str(), "HEAD, GET, OPTIONS");
    os.str("");

    Pistache::Http::Header::Allow a5(Pistache::Http::Method::Head);
    std::vector<Pistache::Http::Method> methods;
    methods.push_back(Pistache::Http::Method::Get);
    a5.addMethods(methods);
    a5.write(os);
    ASSERT_EQ(os.str(), "HEAD, GET");
}

TEST(headers_test, cache_control)
{
    auto testTrivial = [](std::string str,
                          Pistache::Http::CacheDirective::Directive expected) {
        Pistache::Http::Header::CacheControl cc;
        cc.parse(str);

        auto directives = cc.directives();
        ASSERT_EQ(directives.size(), 1U);
        ASSERT_EQ(directives[0].directive(), expected);
    };

    auto testTimed = [](std::string str,
                        Pistache::Http::CacheDirective::Directive expected,
                        uint64_t delta) {
        Pistache::Http::Header::CacheControl cc;
        cc.parse(str);

        auto directives = cc.directives();
        ASSERT_EQ(directives.size(), 1U);

        ASSERT_EQ(directives[0].directive(), expected);
        ASSERT_EQ(directives[0].delta(), std::chrono::seconds(delta));
    };

    testTrivial("no-cache", Pistache::Http::CacheDirective::NoCache);
    testTrivial("no-store", Pistache::Http::CacheDirective::NoStore);
    testTrivial("no-transform", Pistache::Http::CacheDirective::NoTransform);
    testTrivial("only-if-cached", Pistache::Http::CacheDirective::OnlyIfCached);

    testTimed("max-age=0", Pistache::Http::CacheDirective::MaxAge, 0);
    testTimed("max-age=12", Pistache::Http::CacheDirective::MaxAge, 12);

    testTimed("max-stale=12345", Pistache::Http::CacheDirective::MaxStale, 12345);
    testTimed("min-fresh=48", Pistache::Http::CacheDirective::MinFresh, 48);

    Pistache::Http::Header::CacheControl cc1;
    cc1.parse("private, max-age=600");
    auto d1 = cc1.directives();
    ASSERT_EQ(d1.size(), 2U);
    ASSERT_EQ(d1[0].directive(), Pistache::Http::CacheDirective::Private);
    ASSERT_EQ(d1[1].directive(), Pistache::Http::CacheDirective::MaxAge);
    ASSERT_EQ(d1[1].delta(), std::chrono::seconds(600));

    Pistache::Http::Header::CacheControl cc2;
    cc2.parse("public, s-maxage=200, proxy-revalidate");
    auto d2 = cc2.directives();
    ASSERT_EQ(d2.size(), 3U);
    ASSERT_EQ(d2[0].directive(), Pistache::Http::CacheDirective::Public);
    ASSERT_EQ(d2[1].directive(), Pistache::Http::CacheDirective::SMaxAge);
    ASSERT_EQ(d2[1].delta(), std::chrono::seconds(200));
    ASSERT_EQ(d2[2].directive(), Pistache::Http::CacheDirective::ProxyRevalidate);

    Pistache::Http::Header::CacheControl cc3(
        Pistache::Http::CacheDirective::NoCache);
    std::ostringstream oss;
    cc3.write(oss);
    ASSERT_EQ(oss.str(), "no-cache");
    oss.str("");

    cc3.addDirective(Pistache::Http::CacheDirective::NoStore);
    cc3.write(oss);
    ASSERT_EQ(oss.str(), "no-cache, no-store");
    oss.str("");

    Pistache::Http::Header::CacheControl cc4(
        Pistache::Http::CacheDirective::NoTransform);
    cc4.write(oss);
    ASSERT_EQ(oss.str(), "no-transform");
    oss.str("");

    Pistache::Http::Header::CacheControl cc5(
        Pistache::Http::CacheDirective::OnlyIfCached);
    cc5.write(oss);
    ASSERT_EQ(oss.str(), "only-if-cached");
    oss.str("");

    Pistache::Http::Header::CacheControl cc6(
        Pistache::Http::CacheDirective::Private);
    cc6.write(oss);
    ASSERT_EQ(oss.str(), "private");
    oss.str("");

    Pistache::Http::Header::CacheControl cc7(
        Pistache::Http::CacheDirective::Public);
    cc7.write(oss);
    ASSERT_EQ(oss.str(), "public");
    oss.str("");

    Pistache::Http::Header::CacheControl cc8(
        Pistache::Http::CacheDirective::MustRevalidate);
    cc8.write(oss);
    ASSERT_EQ(oss.str(), "must-revalidate");
    oss.str("");

    Pistache::Http::Header::CacheControl cc9(
        Pistache::Http::CacheDirective::ProxyRevalidate);
    cc9.write(oss);
    ASSERT_EQ(oss.str(), "proxy-revalidate");
    oss.str("");

    Pistache::Http::Header::CacheControl cc10(Pistache::Http::CacheDirective(
        Pistache::Http::CacheDirective::MaxStale, std::chrono::seconds(12345)));
    cc10.write(oss);
    ASSERT_EQ(oss.str(), "max-stale=12345");
    oss.str("");

    Pistache::Http::Header::CacheControl cc11(Pistache::Http::CacheDirective(
        Pistache::Http::CacheDirective::MinFresh, std::chrono::seconds(12345)));
    cc11.write(oss);
    ASSERT_EQ(oss.str(), "min-fresh=12345");
    oss.str("");

    Pistache::Http::Header::CacheControl cc14(Pistache::Http::CacheDirective(
        Pistache::Http::CacheDirective::SMaxAge, std::chrono::seconds(12345)));
    cc14.write(oss);
    ASSERT_EQ(oss.str(), "s-maxage=12345");
    oss.str("");

    Pistache::Http::Header::CacheControl cc15(
        Pistache::Http::CacheDirective::Ext);
    cc15.write(oss);
    ASSERT_TRUE(oss.str().empty());
    oss.str("");

    Pistache::Http::Header::CacheControl cc16;
    cc16.write(oss);
    ASSERT_TRUE(oss.str().empty());
    oss.str("");

    Pistache::Http::Header::CacheControl cc12;
    cc12.addDirectives(
        { Pistache::Http::CacheDirective(Pistache::Http::CacheDirective::Public),
          Pistache::Http::CacheDirective(Pistache::Http::CacheDirective::MaxAge,
                                         std::chrono::seconds(600)) });
    cc12.write(oss);
    ASSERT_EQ(oss.str(), "public, max-age=600");
    oss.str("");

    Pistache::Http::Header::CacheControl cc13;
    std::vector<Pistache::Http::CacheDirective> cd;

    cd.emplace_back(Pistache::Http::CacheDirective::Public);
    cd.emplace_back(
        Pistache::Http::CacheDirective::MaxAge, std::chrono::seconds(600));

    cc13.addDirectives(cd);
    cc13.write(oss);
    ASSERT_EQ(oss.str(), "public, max-age=600");
}

TEST(headers_test, content_length)
{
    Pistache::Http::Header::ContentLength cl;
    std::ostringstream oss;
    cl.parse("3495");
    cl.write(oss);

    ASSERT_EQ("3495", oss.str());
    ASSERT_EQ(cl.value(), 3495U);
}

// Verify authorization header with basic method works correctly...
TEST(headers_test, authorization_basic_test)
{
    Pistache::Http::Header::Authorization au;
    std::ostringstream oss;

    // Sample basic method authorization header for credentials
    //  Aladdin:OpenSesame base 64 encoded...
    const std::string BasicEncodedValue = "Basic QWxhZGRpbjpPcGVuU2VzYW1l";

    // Try parsing the raw basic authorization value...
    au.parse(BasicEncodedValue);

    // Verify what went in is what came out...
    au.write(oss);
    ASSERT_EQ(BasicEncodedValue, oss.str());
    oss = std::ostringstream();

    // Verify authorization header recognizes it is basic method and no other...
    ASSERT_TRUE(
        au.hasMethod<Pistache::Http::Header::Authorization::Method::Basic>());
    ASSERT_FALSE(
        au.hasMethod<Pistache::Http::Header::Authorization::Method::Bearer>());

    // Set credentials from decoded user and password...
    au.setBasicUserPassword("Aladdin", "OpenSesame");

    // Verify it encoded correctly...
    au.write(oss);
    ASSERT_EQ(BasicEncodedValue, oss.str());
    oss = std::ostringstream();

    // Verify it decoded correctly...
    ASSERT_EQ(au.getBasicUser(), "Aladdin");
    ASSERT_EQ(au.getBasicPassword(), "OpenSesame");
}

TEST(headers_test, authorization_bearer_test)
{
    Pistache::Http::Header::Authorization au;
    std::ostringstream oss;
    au.parse("Bearer "
             "eyJhbGciOiJIUzUxMiIsInR5cCI6IkpXUyJ9."
             "eyJleHAiOjE1NzA2MzA0MDcsImlhdCI6MTU3MDU0NDAwNywibmFtZSI6IkFkbWluIE5"
             "hbWUiLCJzYW1wbGUiOiJUZXN0In0.zLTAAnBftlqccsU-4mL69P4tQl3VhcglMg-"
             "d0131JxqX4xSZLlO5xMRrCPBgn_00OxKJ9CQdnpjpuzblNQd2-A");
    au.write(oss);

    ASSERT_TRUE(
        au.hasMethod<Pistache::Http::Header::Authorization::Method::Bearer>());
    ASSERT_FALSE(
        au.hasMethod<Pistache::Http::Header::Authorization::Method::Basic>());

    ASSERT_TRUE(
        "Bearer "
        "eyJhbGciOiJIUzUxMiIsInR5cCI6IkpXUyJ9."
        "eyJleHAiOjE1NzA2MzA0MDcsImlhdCI6MTU3MDU0NDAwNywibmFtZSI6IkFkbWluIE5hbWUi"
        "LCJzYW1wbGUiOiJUZXN0In0.zLTAAnBftlqccsU-4mL69P4tQl3VhcglMg-"
        "d0131JxqX4xSZLlO5xMRrCPBgn_00OxKJ9CQdnpjpuzblNQd2-A"
        == oss.str());
    ASSERT_TRUE(
        au.value() == "Bearer "
                      "eyJhbGciOiJIUzUxMiIsInR5cCI6IkpXUyJ9."
                      "eyJleHAiOjE1NzA2MzA0MDcsImlhdCI6MTU3MDU0NDAwNywibmFtZSI6IkFkbWluIE5hbWUi"
                      "LCJzYW1wbGUiOiJUZXN0In0.zLTAAnBftlqccsU-4mL69P4tQl3VhcglMg-"
                      "d0131JxqX4xSZLlO5xMRrCPBgn_00OxKJ9CQdnpjpuzblNQd2-A");
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

    ASSERT_TRUE(oss.str().empty());
    ASSERT_EQ(e.expectation(), Pistache::Http::Expectation::Ext);
    oss.str("");
}

TEST(headers_test, connection)
{
    Pistache::Http::Header::Connection connection;

    struct Test
    {
        const char* data;
        Pistache::Http::ConnectionControl expected;
        const char* expected_string;
    } tests[] = {

        { "close", Pistache::Http::ConnectionControl::Close, "Close" },
        { "clOse", Pistache::Http::ConnectionControl::Close, "Close" },
        { "Close", Pistache::Http::ConnectionControl::Close, "Close" },
        { "CLOSE", Pistache::Http::ConnectionControl::Close, "Close" },

        { "keep-alive", Pistache::Http::ConnectionControl::KeepAlive,
          "Keep-Alive" },
        { "Keep-Alive", Pistache::Http::ConnectionControl::KeepAlive,
          "Keep-Alive" },
        { "kEEp-alIvE", Pistache::Http::ConnectionControl::KeepAlive,
          "Keep-Alive" },
        { "KEEP-ALIVE", Pistache::Http::ConnectionControl::KeepAlive,
          "Keep-Alive" },

        { "Ext", Pistache::Http::ConnectionControl::Ext, "Ext" },
        { "ext", Pistache::Http::ConnectionControl::Ext, "Ext" },
        { "eXt", Pistache::Http::ConnectionControl::Ext, "Ext" },
        { "eXT", Pistache::Http::ConnectionControl::Ext, "Ext" }

    };

    for (auto test : tests)
    {
        Pistache::Http::Header::Connection connection;
        std::ostringstream oss;
        connection.parse(test.data);
        connection.write(oss);

        ASSERT_EQ(connection.control(), test.expected);
        ASSERT_EQ(oss.str(), test.expected_string);
    }
}

TEST(headers_test, date_test_rfc_1123)
{

    using namespace std::chrono;
    Pistache::Http::FullDate::time_point expected_time_point = date::sys_days(date::year { 1994 } / 11 / 6) + hours(8) + minutes(49) + seconds(37);

    /* RFC-1123 */
    Pistache::Http::Header::Date d1;
    d1.parse("Sun, 06 Nov 1994 08:49:37 GMT");
    auto dd1 = d1.fullDate().date();
    ASSERT_EQ(expected_time_point, dd1);
}

TEST(headers_test, date_test_rfc_850)
{

    using namespace std::chrono;
    Pistache::Http::FullDate::time_point expected_time_point = date::sys_days(date::year { 1994 } / 11 / 6) + hours(8) + minutes(49) + seconds(37);

    /* RFC-850 */
    Pistache::Http::Header::Date d2;
    d2.parse("Sunday, 06-Nov-94 08:49:37 GMT");
    auto dd2 = d2.fullDate().date();
    ASSERT_EQ(dd2, expected_time_point);
}

TEST(headers_test, date_test_asctime)
{

    using namespace std::chrono;
    Pistache::Http::FullDate::time_point expected_time_point = date::sys_days(date::year { 1994 } / 11 / 6) + hours(8) + minutes(49) + seconds(37);

    /* ANSI C's asctime format */
    Pistache::Http::Header::Date d3;
    d3.parse("Sun Nov  6 08:49:37 1994");
    auto dd3 = d3.fullDate().date();
    ASSERT_EQ(dd3, expected_time_point);
}

TEST(headers_test, date_test_ostream)
{

    std::ostringstream os;

    Pistache::Http::Header::Date d4;
    d4.parse("Fri, 25 Jan 2019 21:04:45.000000000 UTC");
    d4.write(os);
    const char* cstr_to_compare = "Fri, 25 Jan 2019 21:04:45."
#if defined __clang__ && !defined __linux__
                                  "000000"
#else
                                  "000000000"
#endif
                                  " UTC";

    ASSERT_EQ(cstr_to_compare, os.str());
}

TEST(headers_test, host)
{

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

    /* Due to an error in GLIBC these tests don't fail as expected, further
     * research needed */
    //     ASSERT_THROW( host.parse("256.256.256.256:8080");,
    //     std::invalid_argument); ASSERT_THROW( host.parse("1.0.0.256:8080");,
    //     std::invalid_argument);

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

    /* Due to an error in GLIBC these tests don't fail as expected, further
     * research needed */
    //     ASSERT_THROW(
    //     host.parse("[GGGG:GGGG:GGGG:GGGG:GGGG:GGGG:GGGG:GGGG]:8080");,
    //     std::invalid_argument); ASSERT_THROW( host.parse("[::GGGG]:8080");,
    //     std::invalid_argument);
}

TEST(headers_test, user_agent)
{
    Pistache::Http::Header::UserAgent ua;
    std::ostringstream os;

    ua.parse("CERN-LineMode/2.15 libwww/2.17b3");
    ua.write(os);

    ASSERT_TRUE(
        std::strcmp(os.str().c_str(), "CERN-LineMode/2.15 libwww/2.17b3") == 0);
    ASSERT_EQ(ua.agent(), "CERN-LineMode/2.15 libwww/2.17b3");
}

TEST(headers_test, content_encoding)
{
    Pistache::Http::Header::ContentEncoding ce;
    std::ostringstream oss;

    ce.parse("br");
    ce.write(oss);
    ASSERT_EQ("br", oss.str());
    ASSERT_EQ(ce.encoding(), Pistache::Http::Header::Encoding::Br);
    oss.str("");

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

TEST(headers_test, content_type)
{
    Pistache::Http::Header::ContentType ct;
    std::ostringstream oss;
    ct.parse("text/html; charset=ISO-8859-4");
    ct.write(oss);

    ASSERT_EQ("text/html; charset=ISO-8859-4", oss.str());
    const auto& mime = ct.mime();
    ASSERT_EQ(mime, MIME(Text, Html));
    ASSERT_EQ(mime.getParam("charset").value_or(""), "ISO-8859-4");
}

TEST(headers_test, access_control_allow_origin_test)
{
    Pistache::Http::Header::AccessControlAllowOrigin allowOrigin;
    std::ostringstream os;

    allowOrigin.parse("http://foo.bar");
    allowOrigin.write(os);

    ASSERT_STREQ(os.str().c_str(), "http://foo.bar");
    ASSERT_EQ(allowOrigin.uri(), "http://foo.bar");
}

TEST(headers_test, access_control_allow_headers_test)
{
    Pistache::Http::Header::AccessControlAllowHeaders allowHeaders;
    std::ostringstream os;

    allowHeaders.parse("Content-Type, Access-Control-Allow-Headers, "
                       "Authorization, X-Requested-With");
    allowHeaders.write(os);

    ASSERT_STREQ(os.str().c_str(),
                 "Content-Type, Access-Control-Allow-Headers, "
                 "Authorization, X-Requested-With");
    ASSERT_EQ(allowHeaders.val(), "Content-Type, Access-Control-Allow-Headers, Authorization, "
                                  "X-Requested-With");
}

TEST(headers_test, access_control_expose_headers_test)
{
    Pistache::Http::Header::AccessControlExposeHeaders exposeHeaders;
    std::ostringstream os;

    exposeHeaders.parse("Accept, Location");
    exposeHeaders.write(os);

    ASSERT_EQ(exposeHeaders.val(), "Accept, Location");
    ASSERT_STREQ(os.str().c_str(), "Accept, Location");
}

TEST(headers_test, access_control_allow_methods_test)
{
    Pistache::Http::Header::AccessControlAllowMethods allowMethods;
    std::ostringstream os;

    allowMethods.parse("GET, POST, DELETE");
    allowMethods.write(os);

    ASSERT_EQ(allowMethods.val(), "GET, POST, DELETE");
    ASSERT_STREQ(os.str().c_str(), "GET, POST, DELETE");
}

TEST(headers_test, last_modified_test)
{
    // const std::string ref = "Sun, 06 Nov 1994 08:49:37 GMT";
    using namespace std::chrono;
    Pistache::Http::FullDate::time_point expected_time_point = date::sys_days(date::year { 1994 } / 11 / 6) + hours(8) + minutes(49) + seconds(37);
    Pistache::Http::FullDate fd(expected_time_point);
    Pistache::Http::Header::LastModified l0(fd);
    std::ostringstream oss;
    l0.write(oss);

    // As of July/2024, it seems that in macOS, Linux and OpenBSD this produces
    // an OSS ending "GMT", while in FreeBSD it ends "UTC". Of course, they
    // mean the same thing, and we allow either.
    const bool oss_ends_utc = ((oss.str().length() >= 3) && (oss.str().compare(oss.str().length() - 3, 3, "UTC") == 0));
    const std::string ref(std::string("Sun, 06 Nov 1994 08:49:37 ") + (oss_ends_utc ? "UTC" : "GMT"));

    ASSERT_EQ(ref, oss.str());
    Pistache::Http::Header::LastModified l1;
    l1.parse(ref);
    oss.str("");
    l1.write(oss);
    ASSERT_EQ(ref, oss.str());
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

    std::vector<std::string> tokens({ "server0", "server1" });
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

PISTACHE_CUSTOM_HEADER(TestHeader, "Test-Header")

TEST(headers_test, macro_for_custom_headers)
{
    TestHeader testHeader;
    std::ostringstream os;

    ASSERT_STREQ(TestHeader::Name, "Test-Header");

    testHeader.parse("Header Content Test");
    testHeader.write(os);

    ASSERT_EQ(testHeader.val(), "Header Content Test");
    ASSERT_STREQ(os.str().c_str(), "Header Content Test");
}

TEST(headers_test, add_new_header_test)
{
    const std::string headerName = "Test-Header";

    ASSERT_FALSE(
        Pistache::Http::Header::Registry::instance().isRegistered(headerName));
    Pistache::Http::Header::Registry::instance().registerHeader<TestHeader>();
    ASSERT_TRUE(
        Pistache::Http::Header::Registry::instance().isRegistered(headerName));

    const auto& headersList = Pistache::Http::Header::Registry::instance().headersList();
    const bool isFound      = std::find(headersList.begin(), headersList.end(),
                                        headerName)
        != headersList.end();
    ASSERT_TRUE(isFound);
}

using namespace Pistache::Http::Header;

TEST(headers_test, header_already_registered)
{
    std::string what;

    try
    {
        RegisterHeader(Accept);
    }
    catch (std::exception& e)
    {
        what = e.what();
    }

    ASSERT_EQ("Header already registered", what);
}

TEST(headers_test, unknown_header)
{

    std::string what;

    try
    {
        auto h = Pistache::Http::Header::Registry::instance().makeHeader(
            "UnknownHeader");
    }
    catch (std::exception& e)
    {
        what = e.what();
    }

    ASSERT_EQ("Unknown header", what);
}

TEST(headers_test, could_not_find_header)
{

    std::string what;

    try
    {
        auto h = Pistache::Http::Header::Registry::instance().makeHeader(
            "UnknownHeader");
    }
    catch (std::exception& e)
    {
        what = e.what();
    }

    ASSERT_EQ("Unknown header", what);
}

// Verify registered headers appear in both the client request's strongly typed
//  and raw lists...
TEST(headers_test, registered_header_in_raw_list)
{
    // Make sure TestHeader is registered because Googletest does not guarantee
    //  add_new_header_test will be run before this test...
    if (!Pistache::Http::Header::Registry::instance().isRegistered(
            TestHeader::Name))
        Pistache::Http::Header::Registry::instance().registerHeader<TestHeader>();

    // Verify test header is registered...
    ASSERT_TRUE(Pistache::Http::Header::Registry::instance().isRegistered(
        TestHeader::Name));

    // Prepare a client request header string that should use our registered
    //  TestHeader...
    std::string line = std::string(TestHeader::Name) + ": some data\r\n";

    // Prepare to load the client test header string...
    Pistache::RawStreamBuf<> buf(&line[0], line.size());
    Pistache::StreamCursor cursor(&buf);

    // Simulate server deserializing the client's header request...
    Pistache::Http::Request request;
    Pistache::Http::Private::HeadersStep step(&request);
    step.apply(cursor);

    // Retrieve all of the headers the client submitted in their request...
    const auto& headersCollection = request.headers();

    // Verify our TestHeader is in the strongly typed list...
    ASSERT_TRUE(headersCollection.has<TestHeader>());

    // Obtain the raw header list...
    const auto& rawHeadersList = headersCollection.rawList();

    // Verify the TestHeader is in the raw list as expected...
    const auto foundRawHeader = rawHeadersList.find(TestHeader::Name);
    ASSERT_TRUE(foundRawHeader != rawHeadersList.end());
    ASSERT_EQ(foundRawHeader->second.name(), TestHeader::Name);
    ASSERT_EQ(foundRawHeader->second.value(), "some data");
}

TEST(headers_test, raw_headers_are_case_insensitive)
{
    // no matter the casing of the input header,
    std::vector<std::string> test_cases = {
        "Custom-Header: x\r\n", "CUSTOM-HEADER: x\r\n", "custom-header: x\r\n",
        "CuStOm-HeAdEr: x\r\n"
    };

    for (auto&& test : test_cases)
    {
        Pistache::RawStreamBuf<> buf(&test[0], test.size());
        Pistache::StreamCursor cursor(&buf);
        Pistache::Http::Request request;
        Pistache::Http::Private::HeadersStep step(&request);
        step.apply(cursor);

        // or the header you try and get, it should work:
        ASSERT_TRUE(request.headers().tryGetRaw("Custom-Header").has_value());
        ASSERT_TRUE(request.headers().tryGetRaw("CUSTOM-HEADER").has_value());
        ASSERT_TRUE(request.headers().tryGetRaw("custom-header").has_value());
        ASSERT_TRUE(request.headers().tryGetRaw("CuStOm-HeAdEr").has_value());
    }
}

TEST(headers_test, cookie_headers_are_case_insensitive)
{
    // no matter the casing of the cookie header(s),
    std::vector<std::string> test_cases = {
        "Cookie: x=y\r\n",
        "COOKIE: x=y\r\n",
        "cookie: x=y\r\n",
        "CoOkIe: x=y\r\n",
        "Set-Cookie: x=y\r\n",
        "SET-COOKIE: x=y\r\n",
        "set-cookie: x=y\r\n",
        "SeT-CoOkIe: x=y\r\n",
    };

    for (auto&& test : test_cases)
    {
        Pistache::RawStreamBuf<> buf(&test[0], test.size());
        Pistache::StreamCursor cursor(&buf);
        Pistache::Http::Request request;
        Pistache::Http::Private::HeadersStep step(&request);
        step.apply(cursor);

        // the cookies should still exist.
        ASSERT_TRUE(request.cookies().has("x"));
        ASSERT_EQ(request.cookies().get("x").value, "y");
    }
}
