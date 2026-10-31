// ***************************************************************
//  GTest coverage: xValueRange 的量程与百分比互换
// ***************************************************************
//
// xQwtCurveScaling 按量程画曲线时，每个点的高度是 xValueRange::toPercent()；使用方把选区的
// 百分比换回原值用 fromPercent()；没给量程的曲线用 fromData() 从数据算。这里盖住的是：
//
//  1. 下限对应 0、上限对应 100，量程外的值落到 0-100 之外，换算来回一致。
//  2. 无效量程（上限不大于下限、非有限数）被认出来。
//  3. fromData() 两头按跨度外扩；常数数据也得到非空量程；非有限的输入得到无效量程。
#include "xValueRange.h"

#include <gtest/gtest.h>

#include <limits>

TEST(ValueRange, LowerAndUpperMapTo0And100AndBack) {
    const xValueRange range{200.0, 250.0};
    EXPECT_DOUBLE_EQ(range.toPercent(200.0), 0.0);
    EXPECT_DOUBLE_EQ(range.toPercent(250.0), 100.0);
    EXPECT_DOUBLE_EQ(range.toPercent(225.0), 50.0);
    EXPECT_DOUBLE_EQ(range.fromPercent(20.0), 210.0);
    EXPECT_DOUBLE_EQ(range.fromPercent(range.toPercent(237.5)), 237.5);
}

TEST(ValueRange, ValuesOutsideRangeLandOutside0To100) {
    const xValueRange range{0.0, 10.0};
    EXPECT_DOUBLE_EQ(range.toPercent(-5.0), -50.0);
    EXPECT_DOUBLE_EQ(range.toPercent(15.0), 150.0);
}

TEST(ValueRange, EmptyReversedOrNonFiniteRangesAreInvalid) {
    EXPECT_TRUE((xValueRange{1.0, 2.0}).isValid());
    EXPECT_FALSE((xValueRange{}).isValid());
    EXPECT_FALSE((xValueRange{2.0, 2.0}).isValid());
    EXPECT_FALSE((xValueRange{3.0, 2.0}).isValid());
    EXPECT_FALSE((xValueRange{std::numeric_limits<double>::quiet_NaN(), 2.0}).isValid());
    EXPECT_FALSE((xValueRange{0.0, std::numeric_limits<double>::infinity()}).isValid());
}

TEST(ValueRange, FromDataPadsBothEndsBySpanMargin) {
    const xValueRange range = xValueRange::fromData(100.0, 200.0);
    EXPECT_DOUBLE_EQ(range.lower, 95.0);
    EXPECT_DOUBLE_EQ(range.upper, 205.0);
    const xValueRange wide = xValueRange::fromData(100.0, 200.0, 0.1);
    EXPECT_DOUBLE_EQ(wide.lower, 90.0);
    EXPECT_DOUBLE_EQ(wide.upper, 210.0);
}

TEST(ValueRange, FromDataGivesConstantDataANonEmptyRange) {
    const xValueRange range = xValueRange::fromData(150.0, 150.0);
    EXPECT_TRUE(range.isValid());
    EXPECT_DOUBLE_EQ(range.lower, 142.5);
    EXPECT_DOUBLE_EQ(range.upper, 157.5);
    const xValueRange zero = xValueRange::fromData(0.0, 0.0);
    EXPECT_DOUBLE_EQ(zero.lower, -1.0);
    EXPECT_DOUBLE_EQ(zero.upper, 1.0);
}

TEST(ValueRange, FromDataRejectsNonFiniteOrReversedInput) {
    EXPECT_FALSE(xValueRange::fromData(std::numeric_limits<double>::quiet_NaN(), 1.0).isValid());
    EXPECT_FALSE(xValueRange::fromData(1.0, std::numeric_limits<double>::infinity()).isValid());
    EXPECT_FALSE(xValueRange::fromData(2.0, 1.0).isValid());
}
