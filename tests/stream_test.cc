#include <pistache/stream.h>

#include "gtest/gtest.h"

#include <cstring>

using namespace Pistache;

TEST(stream, test_buffer)
{
    char str[] = "test_string";
    size_t len = strlen(str);
    Buffer buffer1(str, len, false);

    Buffer buffer2 = buffer1.detach(0);
    ASSERT_EQ(buffer2.length, len);
    ASSERT_EQ(buffer2.isDetached, true);

    Buffer buffer3;
    ASSERT_EQ(buffer3.length, 0u);
    ASSERT_EQ(buffer3.isDetached, false);

    Buffer buffer4 = buffer3.detach(0);
    ASSERT_EQ(buffer4.length, 0u);
    ASSERT_EQ(buffer4.isDetached, false);
    
    ASSERT_THROW(buffer1.detach(2 * len);, std::range_error);
}