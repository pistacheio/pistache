#include "gtest/gtest.h"
#include <pistache/string_view.h>

TEST(string_view_test, substr_test) {
    std::string_view orig ("test");
    std::string_view targ ("est");

    std::string_view sub = orig.substr(1);
    ASSERT_EQ(sub, targ);

    sub = orig.substr(1, 10);
    ASSERT_EQ(sub, targ);


    ASSERT_THROW(orig.substr(6), std::out_of_range);
}

TEST(string_view_test, find_test) {
    std::string_view orig ("test");
    std::string_view find ("est");

    ASSERT_EQ(orig.find(find), 1);
    ASSERT_EQ(orig.find(find, 1), 1);
    ASSERT_EQ(orig.find(find, 2), std::size_t(-1));

    ASSERT_EQ(orig.find('e'), 1);
    ASSERT_EQ(orig.find('e', 1), 1);
    ASSERT_EQ(orig.find('e', 2), std::size_t(-1));
    ASSERT_EQ(orig.find('1'), std::size_t(-1));

    ASSERT_EQ(orig.find("est"), 1);
    ASSERT_EQ(orig.find("est", 1), 1);
    ASSERT_EQ(orig.find("est", 1, 2), 1);
    ASSERT_EQ(orig.find("set"), std::size_t(-1));
    ASSERT_EQ(orig.find("est", 2), std::size_t(-1));
    ASSERT_EQ(orig.find("est", 2, 2), std::size_t(-1));
}

TEST(string_view_test, rfind_test) {
    std::string_view orig ("test");
    std::string_view find ("est");

    ASSERT_EQ(orig.rfind(find), 1);
    ASSERT_EQ(orig.rfind(find, 1), 1);

    ASSERT_EQ(orig.rfind('e'), 1);
    ASSERT_EQ(orig.rfind('e', 1), 1);
    ASSERT_EQ(orig.rfind('q'), std::size_t(-1));

    ASSERT_EQ(orig.rfind("est"), 1);
    ASSERT_EQ(orig.rfind("est", 1), 1);
    ASSERT_EQ(orig.rfind("est", 1, 2), 1);
    ASSERT_EQ(orig.rfind("set"), std::size_t(-1));
}

TEST(string_view_test, emptiness) {
    std::string_view e1;
    std::string_view e2 ("");
    std::string_view e3 ("test", 0);
    std::string_view ne ("test");

    ASSERT_TRUE(e1.empty());
    ASSERT_TRUE(e2.empty());
    ASSERT_TRUE(e3.empty());
    ASSERT_FALSE(ne.empty());
}
