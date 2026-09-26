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

#ifndef TEST_VERTICAL_ZOOM_H
#define TEST_VERTICAL_ZOOM_H

// Tier 2: the arithmetic of two fingers on a pane's vertical axis, and
// when a movement of the fingers along an axis counts. Ranges and
// pixels in, ranges and pixels out; no view. That the mapping is the
// pane's own is checked against svgui in TestTouchGestures.

#include "../PinchZoom.h"
#include "../VerticalZoom.h"

#include <QObject>
#include <QtTest>

#include <cmath>
#include <limits>

class TestVerticalZoom : public QObject
{
    Q_OBJECT

    typedef VerticalZoom::Range Range;

    static Range range(double min, double max, bool log) {
        Range r;
        r.min = min;
        r.max = max;
        r.log = log;
        return r;
    }

    static QString text(const Range &r) {
        return QString("%1 to %2 (%3)").arg(r.min).arg(r.max)
            .arg(r.log ? "log" : "linear");
    }

    // The height of the range on its own scale: octaves on a log one
    static double span(const Range &r) {
        return r.log ? std::log2(r.max / r.min) : r.max - r.min;
    }

    static bool near(double a, double b, double tolerance) {
        return std::fabs(a - b) <= tolerance;
    }

private slots:
    // Bottom edge the minimum, top edge the maximum; half way up a log
    // scale the geometric mean, a linear one the arithmetic
    void value_at_y_on_both_scales() {
        Range log = range(40.0, 1500.0, true);
        QCOMPARE(VerticalZoom::valueAtY(log, 400.0, 400.0), 40.0);
        QCOMPARE(VerticalZoom::valueAtY(log, 400.0, 0.0), 1500.0);
        QVERIFY(near(VerticalZoom::valueAtY(log, 400.0, 200.0),
                     std::sqrt(40.0 * 1500.0), 1.0e-9));

        Range linear = range(100.0, 300.0, false);
        QCOMPARE(VerticalZoom::valueAtY(linear, 200.0, 50.0), 250.0);
        QCOMPARE(VerticalZoom::valueAtY(linear, 200.0, 200.0), 100.0);
    }

    void y_for_value_is_the_inverse() {
        for (bool log : { true, false }) {
            Range r = range(55.0, 880.0, log);
            for (double y : { 0.0, 13.5, 200.0, 377.0, 400.0 }) {
                double value = VerticalZoom::valueAtY(r, 400.0, y);
                double back = VerticalZoom::yForValue(r, 400.0, value);
                QVERIFY2(near(back, y, 1.0e-9),
                         qPrintable(QString("%1: y %2 came back as %3")
                                    .arg(text(r)).arg(y).arg(back)));
            }
        }
    }

    // Twice as far in shows half as many octaves, with the value asked
    // for where it was asked for
    void zoom_keeps_the_value_at_y() {
        for (bool log : { true, false }) {
            Range start = range(40.0, 1500.0, log);
            for (double factor : { 0.5, 1.0, 1.7, 2.0, 8.0 }) {
                for (double y : { 0.0, 120.0, 250.0, 400.0 }) {
                    double value = 220.0;
                    Range r = VerticalZoom::zoomedAbout
                        (start, factor, value, 400.0, y);
                    QString what = QString("%1 by %2 about %3 at %4: %5")
                        .arg(text(start)).arg(factor).arg(value).arg(y)
                        .arg(text(r));
                    QVERIFY2(r.log == log, qPrintable(what));
                    QVERIFY2(near(VerticalZoom::yForValue(r, 400.0, value),
                                  y, 1.0e-6), qPrintable(what));
                    QVERIFY2(near(span(r), span(start) / factor,
                                  1.0e-9 * span(start)), qPrintable(what));
                }
            }
        }
    }

    // A factor of one moves the range: what was at 300 is at 200, and
    // the range is as tall as it was. A quarter of the pane is half an
    // octave, which the range goes down by
    void factor_one_scrolls() {
        Range start = range(100.0, 400.0, true);
        double value = VerticalZoom::valueAtY(start, 400.0, 300.0);
        Range r = VerticalZoom::zoomedAbout(start, 1.0, value, 400.0, 200.0);
        QVERIFY(near(VerticalZoom::yForValue(r, 400.0, value), 200.0, 1.0e-6));
        QVERIFY(near(span(r), 2.0, 1.0e-9));
        QVERIFY(near(r.min, 100.0 / std::sqrt(2.0), 1.0e-9));
    }

    void pitch_limits_are_the_piano_and_a_major_third() {
        VerticalZoom::Limits limits = VerticalZoom::pitchLimits();
        QCOMPARE(limits.lowest, 27.5);
        QVERIFY(near(limits.highest, 4186.009, 0.001));
        QVERIFY(near(12.0 * std::log2(limits.narrowest), 4.0, 1.0e-9));
    }

    void a_range_within_the_limits_is_left_as_it_is() {
        VerticalZoom::Limits limits = VerticalZoom::pitchLimits();
        for (bool log : { true, false }) {
            for (Range r : { range(40.0, 1500.0, log),
                             range(27.5, 4186.009, log),
                             range(200.0, 252.0, log) }) {
                r.log = log;
                Range l = VerticalZoom::limited(r, limits, 400.0, 100.0);
                QVERIFY2(l == r, qPrintable(text(r) + " became " + text(l)));
            }
        }
    }

    // Too narrow: widened to a major third about the value at y, which
    // stays there
    void a_narrow_range_is_widened_about_y() {
        VerticalZoom::Limits limits = VerticalZoom::pitchLimits();
        for (bool log : { true, false }) {
            for (double y : { 0.0, 100.0, 400.0 }) {
                Range r = range(200.0, 210.0, log);
                double value = VerticalZoom::valueAtY(r, 400.0, y);
                Range l = VerticalZoom::limited(r, limits, 400.0, y);
                QString what = text(r) + " became " + text(l);
                QVERIFY2(near(l.max / l.min, limits.narrowest, 1.0e-9),
                         qPrintable(what));
                QVERIFY2(near(VerticalZoom::valueAtY(l, 400.0, y), value,
                              1.0e-9), qPrintable(what));
            }
        }
    }

    // Past an end: moved back inside, as tall as it was
    void a_range_past_an_end_is_moved_inside() {
        VerticalZoom::Limits limits = VerticalZoom::pitchLimits();

        Range low = VerticalZoom::limited(range(20.0, 100.0, true),
                                          limits, 400.0, 200.0);
        QCOMPARE(low.min, 27.5);
        QVERIFY(near(low.max, 137.5, 1.0e-6));

        Range high = VerticalZoom::limited(range(3000.0, 6000.0, true),
                                           limits, 400.0, 200.0);
        QCOMPARE(high.max, limits.highest);
        QVERIFY(near(high.min, limits.highest / 2.0, 1.0e-6));

        Range linear = VerticalZoom::limited(range(10.0, 110.0, false),
                                             limits, 400.0, 200.0);
        QCOMPARE(linear.min, 27.5);
        QVERIFY(near(linear.max, 127.5, 1.0e-6));
    }

    // Wider than the limits, or showing nothing: all of them
    void a_wide_or_empty_range_is_all_of_the_limits() {
        VerticalZoom::Limits limits = VerticalZoom::pitchLimits();
        double nan = std::numeric_limits<double>::quiet_NaN();
        for (Range r : { range(10.0, 20000.0, true),
                         range(0.0, 1500.0, true),
                         range(-5.0, 1500.0, true),
                         range(500.0, 500.0, true),
                         range(600.0, 500.0, true),
                         range(nan, 500.0, true),
                         range(10.0, 20000.0, false),
                         range(40.0, nan, false) }) {
            Range l = VerticalZoom::limited(r, limits, 400.0, 200.0);
            QVERIFY2(l.min == limits.lowest && l.max == limits.highest &&
                     l.log == r.log,
                     qPrintable(text(r) + " became " + text(l)));
        }
    }

    // Half way between the lowest and the highest on the range's scale:
    // the geometric mean on a log one, however many are in between
    void middle_is_half_way_between_lowest_and_highest() {
        std::vector<double> values { 110.0, 400.0, 100.0, 105.0, 120.0 };
        double middle = 0.0;
        QVERIFY(VerticalZoom::middleShown(values, range(40.0, 1500.0, true),
                                          middle));
        QVERIFY(near(middle, 200.0, 1.0e-9));
        QVERIFY(VerticalZoom::middleShown(values, range(40.0, 1500.0, false),
                                          middle));
        QVERIFY(near(middle, 250.0, 1.0e-9));

        QVERIFY(VerticalZoom::middleShown({ 73.5 }, range(40.0, 1500.0, true),
                                          middle));
        QVERIFY(near(middle, 73.5, 1.0e-9));
    }

    // Only what the range shows: values above or below it, and not
    // values at all, are not on show. None on show: no middle
    void middle_is_of_what_is_on_show() {
        double nan = std::numeric_limits<double>::quiet_NaN();
        std::vector<double> values { 20.0, 100.0, nan, 400.0, 3000.0, -1.0 };
        double middle = 0.0;
        QVERIFY(VerticalZoom::middleShown(values, range(50.0, 1000.0, true),
                                          middle));
        QVERIFY(near(middle, 200.0, 1.0e-9));
        QVERIFY(VerticalZoom::middleShown(values, range(100.0, 400.0, true),
                                          middle));
        QVERIFY(near(middle, 200.0, 1.0e-9));

        middle = -5.0;
        QVERIFY(!VerticalZoom::middleShown(values, range(500.0, 1000.0, true),
                                           middle));
        QVERIFY(!VerticalZoom::middleShown({}, range(40.0, 1500.0, true),
                                           middle));
        QVERIFY(!VerticalZoom::middleShown(values, range(0.0, 1500.0, true),
                                           middle));
        QVERIFY(!VerticalZoom::middleShown(values, range(500.0, 50.0, false),
                                           middle));
        QCOMPARE(middle, -5.0);
    }

    // Two strays in forty, an octave jump or a breath, leave the middle
    // where the rest put it; five are more than strays
    void a_few_strays_do_not_move_the_middle() {
        std::vector<double> values;
        for (int i = 0; i < 19; ++i) values.push_back(70.0);
        for (int i = 0; i < 19; ++i) values.push_back(90.0);
        values.push_back(700.0);
        values.push_back(35.0);
        double middle = 0.0;
        QVERIFY(VerticalZoom::middleShown(values, range(30.0, 1500.0, true),
                                          middle));
        QVERIFY2(near(middle, std::sqrt(70.0 * 90.0), 1.0e-9),
                 qPrintable(QString("%1").arg(middle)));

        values.clear();
        for (int i = 0; i < 17; ++i) values.push_back(70.0);
        for (int i = 0; i < 18; ++i) values.push_back(90.0);
        for (int i = 0; i < 5; ++i) values.push_back(700.0);
        QVERIFY(VerticalZoom::middleShown(values, range(30.0, 1500.0, true),
                                          middle));
        QVERIFY2(near(middle, std::sqrt(70.0 * 700.0), 1.0e-9),
                 qPrintable(QString("%1").arg(middle)));
    }

    // Zoomed in, a value comes towards the middle of the pane, its
    // distance from there divided by the factor; zoomed out it stays
    void towards_the_middle_by_the_factor() {
        QCOMPARE(VerticalZoom::towardsMiddle(380.0, 400.0, 2.0), 290.0);
        QCOMPARE(VerticalZoom::towardsMiddle(380.0, 400.0, 4.0), 245.0);
        QCOMPARE(VerticalZoom::towardsMiddle(0.0, 400.0, 2.0), 100.0);
        QCOMPARE(VerticalZoom::towardsMiddle(200.0, 400.0, 8.0), 200.0);
        QCOMPARE(VerticalZoom::towardsMiddle(380.0, 400.0, 1.0), 380.0);
        QCOMPARE(VerticalZoom::towardsMiddle(380.0, 400.0, 0.5), 380.0);
    }

    // A low voice, B1 to G2, near the bottom of the default range: zoomed
    // about its middle, which comes towards the middle of the pane, all
    // of it stays in view until it is nearly as tall as the pane. Zoomed
    // about the middle of the pane, it is gone below by three times
    void a_low_pitch_stays_in_view_as_the_range_narrows() {
        const double height = 400.0;
        Range start = range(40.0, 1500.0, true);
        std::vector<double> pitch { 61.7, 73.4, 82.4, 98.0, 65.4, 87.3 };
        double middle = 0.0;
        QVERIFY(VerticalZoom::middleShown(pitch, start, middle));
        double y = VerticalZoom::yForValue(start, height, middle);
        QVERIFY(y > 0.8 * height);

        for (double factor : { 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0 }) {
            Range r = VerticalZoom::zoomedAbout
                (start, factor, middle, height,
                 VerticalZoom::towardsMiddle(y, height, factor));
            double bottom = VerticalZoom::yForValue(r, height, 61.7);
            double top = VerticalZoom::yForValue(r, height, 98.0);
            QVERIFY2(bottom <= height && top >= 0.0,
                     qPrintable(QString("by %1: %2, the pitch from %3 to %4")
                                .arg(factor).arg(text(r)).arg(bottom)
                                .arg(top)));
        }

        double centre = VerticalZoom::valueAtY(start, height, height / 2);
        Range r = VerticalZoom::zoomedAbout(start, 3.0, centre, height,
                                            height / 2);
        QVERIFY(VerticalZoom::yForValue(r, height, 98.0) > height);
    }

    // Nothing within the dead zone; beyond it, all but the dead zone,
    // and then all the way back through it
    void movement_counts_beyond_the_dead_zone() {
        PinchZoom::AxisMovement m(20.0);
        QCOMPARE(m.update(10.0, 0.0), 0.0);
        QCOMPARE(m.update(-20.0, 0.0), 0.0);
        QVERIFY(!m.isCounting());
        QCOMPARE(m.update(25.0, 0.0), 5.0);
        QVERIFY(m.isCounting());
        QCOMPARE(m.update(40.0, 0.0), 20.0);
        QCOMPARE(m.update(0.0, 0.0), -20.0);
        QCOMPARE(m.update(-30.0, 500.0), -50.0);

        PinchZoom::AxisMovement down(20.0);
        QCOMPARE(down.update(-50.0, 0.0), -30.0);
    }

    // Less than half as much as across: not meant
    void movement_across_the_axis_does_not_count() {
        PinchZoom::AxisMovement m(20.0);
        QCOMPARE(m.update(30.0, 100.0), 0.0);
        QCOMPARE(m.update(49.0, -100.0), 0.0);
        QVERIFY(!m.isCounting());
        QCOMPARE(m.update(50.0, 100.0), 30.0);
        QVERIFY(m.isCounting());
    }

    // Started from outside: it counts from where it is, however far
    // that is, so that nothing jumps
    void movement_started_counts_from_there() {
        PinchZoom::AxisMovement m(20.0);
        m.start(8.0);
        QVERIFY(m.isCounting());
        QCOMPARE(m.update(8.0, 0.0), 0.0);
        QCOMPARE(m.update(18.0, 0.0), 10.0);
        m.start(100.0); // already counting: no change
        QCOMPARE(m.update(18.0, 0.0), 10.0);

        PinchZoom::AxisMovement far(20.0);
        far.start(-50.0);
        QCOMPARE(far.update(-50.0, 0.0), 0.0);
        QCOMPARE(far.update(-60.0, 500.0), -10.0);
    }
};

#endif
