#include "gtest/gtest.h"
#include <pistache/string_view.h>

#include <string>

TEST(string_view_test, substr_test) {
  std::string_view orig("test");
  std::string_view targ("est");

  std::string_view sub = orig.substr(1);
  ASSERT_EQ(sub, targ);

  sub = orig.substr(1, 10);
  ASSERT_EQ(sub, targ);

  ASSERT_THROW(orig.substr(6), std::out_of_range);
}

TEST(string_view_test, find_test) {
  std::string_view orig("test");
  std::string_view find("est");

  ASSERT_EQ(orig.find(find), std::size_t(1));
  ASSERT_EQ(orig.find(find, 1), std::size_t(1));
  ASSERT_EQ(orig.find(find, 2), std::size_t(-1));

  ASSERT_EQ(orig.find('e'), std::size_t(1));
  ASSERT_EQ(orig.find('e', 1), std::size_t(1));
  ASSERT_EQ(orig.find('e', 2), std::size_t(-1));
  ASSERT_EQ(orig.find('1'), std::size_t(-1));

  ASSERT_EQ(orig.find("est"), std::size_t(1));
  ASSERT_EQ(orig.find("est", 1), std::size_t(1));
  ASSERT_EQ(orig.find("est", 1, 2), std::size_t(1));
  ASSERT_EQ(orig.find("set"), std::size_t(-1));
  ASSERT_EQ(orig.find("est", 2), std::size_t(-1));
  ASSERT_EQ(orig.find("est", 2, 2), std::size_t(-1));
}

TEST(string_view_test, find_test_2) {
  std::string_view orig1("test");
  std::string_view find1("est");
  ASSERT_EQ(orig1.find(find1, std::size_t(-1)), std::size_t(-1));
  ASSERT_EQ(orig1.find(find1, std::size_t(-1) - 2), std::size_t(-1));

  std::string_view orig2("test");
  std::string_view find2("");
  ASSERT_EQ(orig2.find(find2, std::size_t(6)), std::size_t(-1));
  ASSERT_EQ(orig2.find(find2, std::size_t(2)), std::size_t(2));
  ASSERT_EQ(orig2.find(find2, std::size_t(-1)), std::size_t(-1));

  std::string_view orig3("");
  std::string_view find3("");
  ASSERT_EQ(orig3.find(find3, std::size_t(0)), std::size_t(0));
  ASSERT_EQ(orig3.find(find3, std::size_t(6)), std::size_t(-1));
}

TEST(string_view_test, rfind_test) {
  std::string_view orig("test");
  std::string_view find("est");

  ASSERT_EQ(orig.rfind(find), std::size_t(1));
  ASSERT_EQ(orig.rfind(find, 1), std::size_t(1));

  ASSERT_EQ(orig.rfind('e'), std::size_t(1));
  ASSERT_EQ(orig.rfind('e', 1), std::size_t(1));
  ASSERT_EQ(orig.rfind('q'), std::size_t(-1));

  ASSERT_EQ(orig.rfind("est"), std::size_t(1));
  ASSERT_EQ(orig.rfind("est", 1), std::size_t(1));
  ASSERT_EQ(orig.rfind("est", 1, 2), std::size_t(1));
  ASSERT_EQ(orig.rfind("set"), std::size_t(-1));
}

TEST(string_view_test, rfind_test_2) {
  std::string_view orig1("e");
  std::string_view find1("e");

  ASSERT_EQ(orig1.rfind(find1), std::size_t(0));
  ASSERT_EQ(orig1.rfind(find1, 1), std::size_t(0));

  std::string_view orig2("e");
  std::string_view find2("");

  ASSERT_EQ(orig2.rfind(find2), std::size_t(1));
  ASSERT_EQ(orig2.rfind(find2, 1), std::size_t(1));

  std::string_view orig3("");
  std::string_view find3("e");

  ASSERT_EQ(orig3.rfind(find3), std::size_t(-1));
  ASSERT_EQ(orig3.rfind(find3, 1), std::size_t(-1));

  std::string_view orig4("");
  std::string_view find4("");

  ASSERT_EQ(orig4.rfind(find4), std::size_t(0));
  ASSERT_EQ(orig4.rfind(find4, 1), std::size_t(0));

  std::string_view orig5("a");
  std::string_view find5("b");

  ASSERT_EQ(orig5.rfind(find5), std::size_t(-1));
  ASSERT_EQ(orig5.rfind(find5, 4), std::size_t(-1));
}

TEST(string_view_test, emptiness) {
  std::string_view e1;
  std::string_view e2("");
  std::string_view e3("test", 0);
  std::string_view ne("test");

  ASSERT_TRUE(e1.empty());
  ASSERT_TRUE(e2.empty());
  ASSERT_TRUE(e3.empty());
  ASSERT_FALSE(ne.empty());
}
