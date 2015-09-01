#include "gtest/gtest.h"
#include "http_headers.h"

using namespace Net::Http;

TEST(headers_test, accept) {
    Header::Accept a1;
    a1.parse("audio/*; q=0.2");

    {
        const auto& media = a1.media();
        ASSERT_EQ(media.size(), 1);

        const auto& mime = media[0];
        ASSERT_EQ(mime, MIME(Audio, Star));
        ASSERT_EQ(mime.q().getOrElse(Mime::Q(0)), Mime::Q(20));
    }

    Header::Accept a2;
    a2.parse("text/*, text/html, text/html;level=1, */*");

    {
        const auto& media = a2.media();
        ASSERT_EQ(media.size(), 4);

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

    Header::Accept a3;
    a3.parse("text/*;q=0.3, text/html;q=0.7, text/html;level=1, "
             "text/html;level=2;q=0.4, */*;q=0.5");

    {
        const auto& media = a3.media();
        ASSERT_EQ(media.size(), 5);

        ASSERT_EQ(media[0], MIME(Text, Star));
        ASSERT_EQ(media[0].q().getOrElse(Mime::Q(0)), Mime::Q(30));

        ASSERT_EQ(media[1], MIME(Text, Html));
        ASSERT_EQ(media[2], MIME(Text, Html));
        ASSERT_EQ(media[3], MIME(Text, Html));
        ASSERT_EQ(media[4], MIME(Star, Star));
        ASSERT_EQ(media[4].q().getOrElse(Mime::Q(0)), Mime::Q::fromFloat(0.5));
    }

    Header::Accept a4;
    ASSERT_THROW(a4.parse("text/*;q=0.4, text/html;q=0.3,"), std::runtime_error);

    Header::Accept a5;
    ASSERT_THROW(a4.parse("text/*;q=0.4, text/html;q=0.3, "), std::runtime_error);

}

TEST(headers_test, content_length) {
    Header::ContentLength cl;

    cl.parse("3495");
    ASSERT_EQ(cl.value(), 3495);
}

TEST(headers_test, host) {
    Header::Host host;

    host.parse("www.w3.org");
    ASSERT_EQ(host.host(), "www.w3.org");
    ASSERT_EQ(host.port(), 80);

    host.parse("localhost:8080");
    ASSERT_EQ(host.host(), "localhost");
    ASSERT_EQ(host.port(), 8080);
}

TEST(headers_test, user_agent) {
    Header::UserAgent ua;

    ua.parse("CERN-LineMode/2.15 libwww/2.17b3");
    ASSERT_EQ(ua.ua(), "CERN-LineMode/2.15 libwww/2.17b3");
}

TEST(headers_test, content_encoding) {
    Header::ContentEncoding ce;

    ce.parse("gzip");
    ASSERT_EQ(ce.encoding(), Header::Encoding::Gzip);
}

TEST(headers_test, content_type) {
    Header::ContentType ct;

    ct.parse("text/html; charset=ISO-8859-4");
    const auto& mime = ct.mime();
    ASSERT_EQ(mime, MIME(Text, Html));
    ASSERT_EQ(mime.getParam("charset").getOrElse(""), "ISO-8859-4");
}
