/*
 * SPDX-FileCopyrightText: 2018 jcastro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>
#include <pistache/mailbox.h>

struct Data
{
    static inline int num_instances = 0;
    static constexpr int fingerprint = 0xdeadbeef;

    Data()
        : val(Data::fingerprint)
        , payload(std::string(100, 'x'))
    {
        num_instances++;
    }

    Data(Data&&)
        : val(Data::fingerprint)
        , payload(std::string(100, 'x'))
    {
        num_instances++;
    }

    Data(const Data&) = delete;

    ~Data()
    {
        EXPECT_EQ(val, Data::fingerprint);
        EXPECT_GE(--num_instances, 0);
    }

    int val;

    // Dynamic allocation is required to detect a potential memory leak here
    std::string payload;
};

class QueueTest : public testing::Test
{
public:
    void SetUp() override
    {
        Data::num_instances = 0;
    }
};

TEST_F(QueueTest, destructor_test)
{
    Pistache::Queue<Data> queue;
    EXPECT_TRUE(queue.empty());

    for (int i = 0; i < 5; i++)
    {
        queue.push(Data());
    }
    // Should call Data::~Data 5 times and not 6 (placeholder entry)
}

TEST_F(QueueTest, push_pop)
{
    auto queue = std::make_unique<Pistache::Queue<Data>>();
    EXPECT_TRUE(queue->empty());

    for (int i = 0; i < 5; i++)
    {
        queue->push(Data());
    }

    for (int i = 0; i < 5; i++)
    {
        EXPECT_NE(queue->popSafe(), nullptr);
    }

    EXPECT_TRUE(queue->empty());
    EXPECT_EQ(Data::num_instances, 0);
}
