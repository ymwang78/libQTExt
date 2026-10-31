// ***************************************************************
//  GTest coverage: xTimeAxisFormat 的时间轴刻度精简格式
// ***************************************************************
//
// xQwtDateScaleDraw 画时间轴时，每个刻度的文字由 xTimeAxisLabel() 给出，"前一个刻度"由
// xPreviousTickIndex() 找。这里盖住的是：
//
//  1. 只用数字、只占一行，不再出现 Qwt 默认的 "周四 06 10月 2016"。
//  2. 进位规则：天 / 周级只在第一个刻度和跨年处带年份；时分秒级只在第一个刻度和跨日处
//     带月-日；月级总带年份；小时是 24 小时制。
//  3. 前一个刻度按值找最近的更早刻度，与刻度列表的顺序无关；第一个刻度没有前一个。
//  4. 两者合起来，把截图里那条周级时间轴画成 "2016-10-06  10-13  10-20 ..."。
#include "xTimeAxisFormat.h"

#include <gtest/gtest.h>

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QTime>

#include <string>
#include <vector>

namespace {

QDateTime at(int year, int month, int day, int hour = 0, int minute = 0, int second = 0,
             int msec = 0) {
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second, msec));
}

std::string label(const QDateTime& tick, const QDateTime& prev, xTimeUnit unit) {
    return xTimeAxisLabel(tick, prev, unit).toStdString();
}

}  // namespace

TEST(TimeAxisFormat, DayTicksCarryYearOnlyOnFirstTickAndYearChange) {
    EXPECT_EQ(label(at(2016, 10, 6), QDateTime(), xTimeUnit::Day), "2016-10-06");
    EXPECT_EQ(label(at(2016, 10, 13), at(2016, 10, 6), xTimeUnit::Day), "10-13");
    EXPECT_EQ(label(at(2016, 12, 29), at(2016, 12, 22), xTimeUnit::Week), "12-29");
    EXPECT_EQ(label(at(2017, 1, 5), at(2016, 12, 29), xTimeUnit::Week), "2017-01-05");
}

TEST(TimeAxisFormat, HourAndMinuteTicksCarryMonthDayOnlyOnFirstTickAndDayChange) {
    EXPECT_EQ(label(at(2016, 10, 6, 0), QDateTime(), xTimeUnit::Hour), "10-06 00:00");
    EXPECT_EQ(label(at(2016, 10, 6, 18), at(2016, 10, 6, 12), xTimeUnit::Hour), "18:00");
    EXPECT_EQ(label(at(2016, 10, 7, 0), at(2016, 10, 6, 18), xTimeUnit::Hour), "10-07 00:00");
    EXPECT_EQ(label(at(2016, 10, 6, 8, 30), at(2016, 10, 6, 8, 15), xTimeUnit::Minute), "08:30");
}

TEST(TimeAxisFormat, SecondAndMillisecondTicksKeepSubMinuteDigits) {
    EXPECT_EQ(label(at(2016, 10, 6, 8, 0, 15), at(2016, 10, 6, 8, 0, 10), xTimeUnit::Second),
              "08:00:15");
    EXPECT_EQ(label(at(2016, 10, 6, 8, 0, 15), QDateTime(), xTimeUnit::Second), "10-06 08:00:15");
    EXPECT_EQ(label(at(2016, 10, 6, 8, 0, 15, 250), at(2016, 10, 6, 8, 0, 15, 200),
                    xTimeUnit::Millisecond),
              "08:00:15.250");
}

TEST(TimeAxisFormat, MonthTicksAlwaysCarryYear) {
    EXPECT_EQ(label(at(2016, 10, 1), QDateTime(), xTimeUnit::Month), "2016-10");
    EXPECT_EQ(label(at(2016, 11, 1), at(2016, 10, 1), xTimeUnit::Month), "2016-11");
}

TEST(TimeAxisFormat, YearTicksShowYearOnly) {
    EXPECT_EQ(label(at(2017, 1, 1), at(2016, 1, 1), xTimeUnit::Year), "2017");
}

TEST(TimeAxisFormat, PreviousTickIndexPicksNearestEarlierTickInAnyOrder) {
    const QList<double> ticks{30.0, 10.0, 20.0};
    EXPECT_EQ(xPreviousTickIndex(ticks, 10.0), -1);
    EXPECT_EQ(xPreviousTickIndex(ticks, 20.0), 1);
    EXPECT_EQ(xPreviousTickIndex(ticks, 30.0), 2);
    EXPECT_EQ(xPreviousTickIndex(ticks, 25.0), 2);
    EXPECT_EQ(xPreviousTickIndex({}, 25.0), -1);
}

TEST(TimeAxisFormat, WeeklyAxisFromScreenshotBecomesCompact) {
    // 截图里那条轴: 2016-10-06 起每周一个刻度, 刻度值是 Qwt 约定的毫秒数
    QList<double> ticks;
    for (int week = 0; week < 9; ++week)
        ticks.append(static_cast<double>(at(2016, 10, 6).addDays(7 * week).toMSecsSinceEpoch()));

    std::vector<std::string> labels;
    for (double value : ticks) {
        const int prev = xPreviousTickIndex(ticks, value);
        const QDateTime prev_time =
            prev >= 0 ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(ticks[prev]))
                      : QDateTime();
        labels.push_back(label(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(value)),
                               prev_time, xTimeUnit::Week));
    }
    const std::vector<std::string> expected{"2016-10-06", "10-13", "10-20", "10-27", "11-03",
                                            "11-10",      "11-17", "11-24", "12-01"};
    EXPECT_EQ(labels, expected);
}
