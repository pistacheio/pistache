#include "gtest/gtest.h"
#include "http_header.h"

using namespace Net::Http::Mime;

TEST(mime_test, basic_test) {
    MediaType m1(Type::Text, Subtype::Plain);
    ASSERT_EQ(m1.top(), Type::Text);
    ASSERT_EQ(m1.sub(), Subtype::Plain);
    ASSERT_EQ(m1.suffix(), Suffix::None);
    ASSERT_EQ(m1.toString(), "text/plain");

    ASSERT_EQ(m1, MIME(Text, Plain));

    auto m2 = MIME3(Application, Xhtml, Xml);
    ASSERT_EQ(m2.toString(), "application/xhtml+xml");

    auto m3 = MIME(Text, Plain);
    ASSERT_TRUE(m3.q().isEmpty());
    m3.setQuality(Q::fromFloat(0.7));
    ASSERT_EQ(m3.q().getOrElse(Q(0)), Q(70));

    ASSERT_EQ(m3.toString(), "text/plain; q=0.7");

    auto m4 = MIME3(Application, Json, Zip);
    m4.setQuality(Q::fromFloat(0.79));

    ASSERT_EQ(m4.toString(), "application/json+zip; q=0.79");
}

TEST(mime_test, valid_parsing_test) {
    auto m1 = MediaType::fromString("application/json");
    ASSERT_EQ(m1, MIME(Application, Json));
    ASSERT_TRUE(m1.q().isEmpty());

    auto m2 = MediaType::fromString("application/xhtml+xml");
    ASSERT_EQ(m2, MediaType(Type::Application, Subtype::Xhtml, Suffix::Xml));
    ASSERT_TRUE(m2.q().isEmpty());

    auto m3 = MediaType::fromString("application/json; q=0.3");
    ASSERT_EQ(m3, MIME(Application, Json));
    ASSERT_EQ(m3.q().getOrElse(Q(0)), Q::fromFloat(0.3));

    auto m4 = MediaType::fromString("application/xhtml+xml; q=0.7");
    ASSERT_EQ(m4.top(), Type::Application);
    ASSERT_EQ(m4.sub(), Subtype::Xhtml);
    ASSERT_EQ(m4.suffix(), Suffix::Xml);
    ASSERT_EQ(m4.q().getOrElse(Q(0)), Q(70));

    auto m5 = MediaType::fromString("application/xhtml+xml; q=0.78");
    ASSERT_EQ(m5.q().getOrElse(Q(0)), Q(78));

    auto m6 = MediaType::fromString("application/vnd.adobe.flash-movie");
    ASSERT_EQ(m6.top(), Type::Application);
    ASSERT_EQ(m6.sub(), Subtype::Vendor);
    ASSERT_EQ(m6.suffix(), Suffix::None);

    auto m7 = MediaType::fromString("application/vnd.mycompany.myapp-v2+json");
    ASSERT_EQ(m7.top(), Type::Application);
    ASSERT_EQ(m7.sub(), Subtype::Vendor);
    ASSERT_EQ(m7.suffix(), Suffix::Json);
}
