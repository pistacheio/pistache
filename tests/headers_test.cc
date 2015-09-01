#include "gtest/gtest.h"
#include "http_headers.h"

using namespace Net::Http;

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
