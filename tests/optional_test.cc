#include "gtest/gtest.h"

#include <pistache/optional.h>

using Pistache::Optional;

TEST(optional, constructor) {
    Optional<bool> value(Pistache::Some(true));
    ASSERT_FALSE(value.isEmpty());

    EXPECT_TRUE(value.get());
}

TEST(optional, copy_constructor) {
    Optional<bool> value(Pistache::Some(true));
    ASSERT_FALSE(value.isEmpty());
    EXPECT_TRUE(value.get());

    Optional<bool> copy_constructed(value);
    ASSERT_FALSE(copy_constructed.isEmpty());
    EXPECT_TRUE(copy_constructed.get());
}

TEST(optional, copy_assignment_operator) {
    Optional<bool> value(Pistache::Some(true));
    ASSERT_FALSE(value.isEmpty());

    Optional<bool> other;
    EXPECT_TRUE(other.isEmpty());

    other = value;
    ASSERT_FALSE(other.isEmpty());

    EXPECT_TRUE(other.get());
}

TEST(optional, copy_assignment_operator_for_convertible_type) {
    Optional<bool> value;
    EXPECT_TRUE(value.isEmpty());

    value = Pistache::Some(true);
    ASSERT_FALSE(value.isEmpty());

    EXPECT_TRUE(value.get());
}

TEST(optional, copy_assignment_operator_for_self_assignment) {
    Optional<bool> value(Pistache::Some(true));
    ASSERT_FALSE(value.isEmpty());
    EXPECT_TRUE(value.get());

    value = value;
    ASSERT_FALSE(value.isEmpty());
    EXPECT_TRUE(value.get());
}

TEST(optional, move_constructor) {
    Optional<bool> value(Pistache::Some(true));
    ASSERT_FALSE(value.isEmpty());
    EXPECT_TRUE(value.get());

    Optional<bool> value_from_move(std::move(value));
    ASSERT_FALSE(value_from_move.isEmpty());
    EXPECT_TRUE(value_from_move.get());
}

TEST(optional, move_assignment_operator) {
    Optional<bool> value(Pistache::Some(true));
    ASSERT_FALSE(value.isEmpty());
    EXPECT_TRUE(value.get());

    Optional<bool> move_assigned;
    move_assigned = std::move(value);
    ASSERT_FALSE(move_assigned.isEmpty());
    EXPECT_TRUE(move_assigned.get());
}

TEST(optional, constructor_none) {
    Optional<bool> value(Pistache::None());
    EXPECT_TRUE(value.isEmpty());
}

TEST(optional, copy_constructor_none) {
    Optional<bool> value(Pistache::None());
    EXPECT_TRUE(value.isEmpty());

    Optional<bool> copy_constructed(value);
    EXPECT_TRUE(value.isEmpty());
}

TEST(optional, copy_assignment_operator_none) {
    Optional<bool> value(Pistache::None());
    EXPECT_TRUE(value.isEmpty());

    Optional<bool> assigned = Pistache::None();
    EXPECT_TRUE(assigned.isEmpty());
}

TEST(optional, move_constructor_none) {
    Optional<bool> value(Pistache::None());
    EXPECT_TRUE(value.isEmpty());

    Optional<bool> move_constructed(std::move(value));
    EXPECT_TRUE(move_constructed.isEmpty());
}

TEST(optional, move_assignment_operator_none) {
    Optional<bool> value(Pistache::None());
    EXPECT_TRUE(value.isEmpty());

    Optional<bool> move_assigned(std::move(value));
    EXPECT_TRUE(move_assigned.isEmpty());
}
