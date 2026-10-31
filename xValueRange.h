#pragma once
// ***************************************************************
//  xValueRange   version:  1.0   -  date:  2026/10/31
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  一条曲线的量程 (下限 / 上限) 与百分比的互换。xQwtCurveScaling
//  (xQwtChart.h) 用它把多条曲线按各自量程画成 0-100%, 使用方也用它把
//  选区的百分比换回原值。只有头文件、不依赖 Qt。
// ***************************************************************
#include <cmath>

struct xValueRange {
    double lower = 0.0;
    double upper = 0.0;

    bool isValid() const { return std::isfinite(lower) && std::isfinite(upper) && upper > lower; }

    // 原值 → 量程百分比: 下限 0, 上限 100, 量程外的值落在 0-100 之外。量程须有效。
    double toPercent(double value) const { return (value - lower) / (upper - lower) * 100.0; }
    double fromPercent(double percent) const { return lower + percent / 100.0 * (upper - lower); }

    // 由数据的最小 / 最大值得到量程, 两头各外扩跨度的 margin 倍; 常数数据外扩 |值| 的 margin 倍
    // (值为 0 时外扩 1), 保证量程不为空。最小 / 最大值不是有限数时返回无效量程。
    static xValueRange fromData(double min_value, double max_value, double margin = 0.05) {
        if (!std::isfinite(min_value) || !std::isfinite(max_value) || max_value < min_value)
            return {};
        double pad = (max_value - min_value) * margin;
        if (pad <= 0.0) pad = min_value != 0.0 ? std::abs(min_value) * margin : 1.0;
        return {min_value - pad, max_value + pad};
    }
};
