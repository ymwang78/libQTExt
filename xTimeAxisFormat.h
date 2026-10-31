#pragma once
// ***************************************************************
//  xTimeAxisFormat   version:  1.0   -  date:  2026/10/31
//  -------------------------------------------------------------
//  时间轴刻度文字的精简格式: 只用数字、只占一行, 只在第一个刻度和
//  跨入更大单位 (跨年 / 跨日) 的刻度上带出那个单位, 例如
//      天级:  2016-10-06  10-13  10-20 ... 12-29  2017-01-05
//      时级:  10-06 00:00  06:00  12:00  18:00  10-07 00:00
//  只依赖 QtCore。xQwtDateScaleDraw (xQwtChart.h) 用它画 Qwt 毫秒
//  时间轴; 横轴是秒或样本序号的图表, 换算成 QDateTime 后也可直接调用。
//  全部写在头文件里: 各使用者按文件清单编译 libQTExt, 不必为它改构建。
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
// ***************************************************************
#include <QDateTime>
#include <QList>
#include <QString>

// 刻度间隔, 与 QwtDate::IntervalType 同序
enum class xTimeUnit { Millisecond, Second, Minute, Hour, Day, Week, Month, Year };

// 一个刻度的文字。prev 是前一个刻度的时刻, 第一个刻度传无效的 QDateTime。
// 天 / 周级在第一个刻度和跨年处带年份; 时分秒级在第一个刻度和跨日处带月-日;
// 月级总带年份 (单独一个月份数字看不懂)。
inline QString xTimeAxisLabel(const QDateTime& tick, const QDateTime& prev, xTimeUnit unit) {
    const bool first = !prev.isValid();
    switch (unit) {
    case xTimeUnit::Year:
        return tick.toString(QStringLiteral("yyyy"));
    case xTimeUnit::Month:
        return tick.toString(QStringLiteral("yyyy-MM"));
    case xTimeUnit::Week:
    case xTimeUnit::Day: {
        const bool new_year = first || prev.date().year() != tick.date().year();
        return tick.toString(new_year ? QStringLiteral("yyyy-MM-dd") : QStringLiteral("MM-dd"));
    }
    default:
        break;
    }
    QString time_format;
    switch (unit) {
    case xTimeUnit::Millisecond: time_format = QStringLiteral("HH:mm:ss.zzz"); break;
    case xTimeUnit::Second:      time_format = QStringLiteral("HH:mm:ss");     break;
    default:                     time_format = QStringLiteral("HH:mm");        break;
    }
    const bool new_day = first || prev.date() != tick.date();
    return tick.toString(new_day ? QStringLiteral("MM-dd ") + time_format : time_format);
}

// ticks 里排在 value 前面、离它最近的那个刻度的下标; value 是第一个刻度时返回 -1。
// value 应取自 ticks 本身 (刻度绘制时就是这样), 所以按精确值比较; ticks 不要求有序。
inline int xPreviousTickIndex(const QList<double>& ticks, double value) {
    int index = -1;
    for (int i = 0; i < ticks.size(); ++i) {
        if (ticks[i] < value && (index < 0 || ticks[i] > ticks[index])) index = i;
    }
    return index;
}
