#include "gtest/gtest.h"
#include <pistache/http.h>
#include <pistache/endpoint.h>

using namespace Pistache;

void setThreadNameTest(const std::string pThreadName)
{
    std::shared_ptr<Http::Endpoint> mHttpEndpoint_;
    Address addr(Ipv4::any(), Pistache::Port(0));
    mHttpEndpoint_ = std::make_shared<Http::Endpoint>(addr);

    auto test_options = Http::Endpoint::options()
          .threads(2)
          .threadsName(pThreadName);
    mHttpEndpoint_->init(test_options);
}

TEST(set_threadname_test, thread_naming_test)
{
    const std::string null_str = "";
    const std::string single_char = "a";
    const std::string max_length = "0123456789abcdef";
    const std::string exceed_length = "0123456789abcdefghi";

    EXPECT_NO_THROW(setThreadNameTest(null_str));
    EXPECT_NO_THROW(setThreadNameTest(single_char));
    EXPECT_NO_THROW(setThreadNameTest(max_length));
    EXPECT_NO_THROW(setThreadNameTest(exceed_length));
}
