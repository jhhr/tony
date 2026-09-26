/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Tony
    An intonation analysis and annotation tool
    Centre for Digital Music, Queen Mary, University of London.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "VerticalZoom.h"

#include <algorithm>
#include <cmath>

namespace VerticalZoom
{

namespace {

// Onto the axis the pane spaces evenly: log10, as svgui's LogRange
double map(bool log, double value)
{
    return log ? std::log10(value) : value;
}

double unmap(bool log, double point)
{
    return log ? std::pow(10.0, point) : point;
}

// False for NaN as well
bool shows(const Range &range)
{
    return range.max > range.min && (!range.log || range.min > 0.0);
}

}

Limits
pitchLimits()
{
    Limits limits;
    limits.lowest = 27.5;                            // A0
    limits.highest = 4186.009;                       // C8
    limits.narrowest = std::pow(2.0, 4.0 / 12.0);    // a major third
    return limits;
}

double
valueAtY(const Range &range, double height, double y)
{
    double bottom = map(range.log, range.min);
    double top = map(range.log, range.max);
    return unmap(range.log, bottom + (top - bottom) * (height - y) / height);
}

double
yForValue(const Range &range, double height, double value)
{
    double bottom = map(range.log, range.min);
    double top = map(range.log, range.max);
    return height - height * (map(range.log, value) - bottom) / (top - bottom);
}

Range
zoomedAbout(const Range &range, double factor, double value,
            double height, double y)
{
    double span = (map(range.log, range.max) - map(range.log, range.min))
        / factor;
    double bottom = map(range.log, value) - span * (height - y) / height;

    Range zoomed;
    zoomed.min = unmap(range.log, bottom);
    zoomed.max = unmap(range.log, bottom + span);
    zoomed.log = range.log;
    return zoomed;
}

Range
limited(const Range &range, const Limits &limits, double height, double y)
{
    bool log = range.log;

    Range all;
    all.min = limits.lowest;
    all.max = limits.highest;
    all.log = log;

    if (!shows(range)) return all;

    Range result = range;

    if (result.max < result.min * limits.narrowest) {

        // Widened about what is at y, or at the edge the fingers are
        // beyond, as far as the limits have it
        double at = std::min(std::max(y, 0.0), height);
        double value = std::min(std::max(valueAtY(result, height, at),
                                         limits.lowest),
                                limits.highest);

        if (log) {
            double span = std::log10(result.max / result.min);
            result = zoomedAbout(result,
                                 span / std::log10(limits.narrowest),
                                 value, height, at);
        } else {
            // min + (max - min) * above = value, with max = narrowest * min
            double above = (height - at) / height;
            result.min = value / (1.0 + above * (limits.narrowest - 1.0));
            result.max = result.min * limits.narrowest;
        }
    }

    double span = map(log, result.max) - map(log, result.min);
    double lowest = map(log, limits.lowest);
    double highest = map(log, limits.highest);

    if (span >= highest - lowest) return all;

    // Moved along the mapped axis, so that it keeps its size on screen
    double shift = 0.0;
    if (map(log, result.min) < lowest) {
        shift = lowest - map(log, result.min);
    } else if (map(log, result.max) > highest) {
        shift = highest - map(log, result.max);
    }

    if (shift != 0.0) {
        double bottom = map(log, result.min) + shift;
        result.min = unmap(log, bottom);
        result.max = unmap(log, bottom + span);
        // Exactly on the limit, not a rounding error either side of it
        if (shift > 0.0) result.min = limits.lowest;
        else result.max = limits.highest;
    }

    return result;
}

bool
middleShown(const std::vector<double> &values, const Range &range,
            double &middle)
{
    if (!shows(range)) return false;

    std::vector<double> shown;
    shown.reserve(values.size());
    for (double value : values) {
        // False for NaN as well
        if (value >= range.min && value <= range.max) {
            shown.push_back(map(range.log, value));
        }
    }
    if (shown.empty()) return false;

    // An octave jump at the start of a note, or a breath taken for a
    // pitch, is not what is sung: a few points either way are left out
    size_t stray = shown.size() / 20;
    auto lowest = shown.begin() + stray;
    auto highest = shown.end() - 1 - stray;
    std::nth_element(shown.begin(), lowest, shown.end());
    double low = *lowest;
    std::nth_element(shown.begin(), highest, shown.end());
    double high = *highest;

    middle = unmap(range.log, (low + high) / 2.0);
    return true;
}

double
towardsMiddle(double y, double height, double factor)
{
    if (!(factor > 1.0)) return y;
    double middle = height / 2.0;
    return middle + (y - middle) / factor;
}

}
