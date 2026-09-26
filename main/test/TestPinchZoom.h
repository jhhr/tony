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

#ifndef TEST_PINCH_ZOOM_H
#define TEST_PINCH_ZOOM_H

// Tier 2: the arithmetic of a pinch on a pane's time axis. Zoom levels
// and frames in, zoom levels and frames out; no view.

#include "../PinchZoom.h"

#include "data/model/RelativelyFineZoomConstraint.h"

#include <QObject>
#include <QtTest>

#include <cmath>
#include <limits>

class TestPinchZoom : public QObject
{
    Q_OBJECT

    typedef sv::ZoomLevel ZoomLevel;
    typedef sv::sv_frame_t frame_t;

    static ZoomLevel fpp(int level) {
        return ZoomLevel(ZoomLevel::FramesPerPixel, level);
    }

    static ZoomLevel ppf(int level) {
        return ZoomLevel(ZoomLevel::PixelsPerFrame, level);
    }

    static QString text(ZoomLevel z) {
        return QString("%1 %2")
            .arg(z.zone == ZoomLevel::FramesPerPixel ? "fpp" : "ppf")
            .arg(z.level);
    }

private slots:
    void frames_per_pixel_in_both_zones() {
        QCOMPARE(PinchZoom::framesPerPixel(fpp(64)), 64.0);
        QCOMPARE(PinchZoom::framesPerPixel(fpp(1)), 1.0);
        QCOMPARE(PinchZoom::framesPerPixel(ppf(4)), 0.25);
    }

    // Every level the mouse wheel steps through, from far out to as far
    // in as it goes, is one a pinch can land on exactly
    void pinch_levels_are_the_wheel_levels() {
        sv::RelativelyFineZoomConstraint constraint;
        ZoomLevel level = constraint.getNearestZoomLevel(fpp(65536));
        int steps = 0;
        while (true) {
            ZoomLevel found =
                PinchZoom::nearestLevel(PinchZoom::framesPerPixel(level));
            QVERIFY2(found == level,
                     qPrintable(text(level) + " came back as " +
                                text(found)));
            ZoomLevel next = constraint.getNearestZoomLevel
                (level.decremented(), sv::ZoomConstraint::RoundDown);
            if (next == level) break;
            level = next;
            ++steps;
        }
        QVERIFY(steps > 50);
        QVERIFY(level == constraint.getMinZoomLevel());
    }

    void nearest_level_between_two() {
        QVERIFY(PinchZoom::nearestLevel(33.0) == fpp(32));
        QVERIFY(PinchZoom::nearestLevel(0.26) == ppf(4));
        QVERIFY(PinchZoom::nearestLevel(1.0) == fpp(1));
    }

    // As far as the wheel goes and no further, whatever is asked
    void nearest_level_is_limited_as_the_wheel_is() {
        sv::RelativelyFineZoomConstraint constraint;
        QVERIFY(PinchZoom::nearestLevel(1.0e12) ==
                constraint.getMaxZoomLevel());
        QVERIFY(PinchZoom::nearestLevel(1.0e-6) ==
                constraint.getMinZoomLevel());
        QVERIFY(PinchZoom::nearestLevel(0.0) ==
                constraint.getMinZoomLevel());
        QVERIFY(PinchZoom::nearestLevel
                (std::numeric_limits<double>::quiet_NaN()) ==
                constraint.getMinZoomLevel());
    }

    // 64 and 72 are neighbours. Between them, whichever the view is at
    // stays until the other is nearer by 2%: at 67.2 and 68.6 frames per
    // pixel
    void pinch_between_two_levels_stays_where_it_is() {
        for (double target : { 67.3, 68.0, 68.5 }) {
            QVERIFY2(PinchZoom::pinchedLevel(fpp(64), target) == fpp(64),
                     qPrintable(QString("from 64 at %1").arg(target)));
            QVERIFY2(PinchZoom::pinchedLevel(fpp(72), target) == fpp(72),
                     qPrintable(QString("from 72 at %1").arg(target)));
        }
        QVERIFY(PinchZoom::pinchedLevel(fpp(64), 68.8) == fpp(72));
        QVERIFY(PinchZoom::pinchedLevel(fpp(72), 67.0) == fpp(64));
    }

    void pinch_goes_as_far_as_it_asks() {
        QVERIFY(PinchZoom::pinchedLevel(fpp(64), 16.0) == fpp(16));
        QVERIFY(PinchZoom::pinchedLevel(fpp(64), 256.0) == fpp(256));
        QVERIFY(PinchZoom::pinchedLevel(fpp(2), 0.25) == ppf(4));
        QVERIFY(PinchZoom::pinchedLevel(fpp(64), 0.0) == fpp(64));
    }

    // View::getFrameForX(): from the centre rounded down to a whole
    // pixel, and width / 2 rounded down
    void frame_at_x_as_the_view_has_it() {
        QCOMPARE(PinchZoom::frameAtX(10030, fpp(64), 1000, 500), 9984.0);
        QCOMPARE(PinchZoom::frameAtX(10030, fpp(64), 1000, 600),
                 9984.0 + 6400.0);
        QCOMPARE(PinchZoom::frameAtX(10030, fpp(64), 999, 499), 9984.0);
        QCOMPARE(PinchZoom::frameAtX(10030, fpp(64), 1000, 499.5),
                 9984.0 - 32.0);
        QCOMPARE(PinchZoom::frameAtX(1000, ppf(4), 1000, 520), 1005.0);
    }

    // What centreFor() puts at x is shown there, to within a pixel
    void centre_for_puts_the_frame_at_x() {
        for (ZoomLevel z : { fpp(64), fpp(7), fpp(1), ppf(4) }) {
            for (double x : { 0.0, 250.0, 500.0, 731.5, 999.0 }) {
                for (double frame : { 100000.0, 123457.0 }) {
                    frame_t centre = PinchZoom::centreFor(frame, z, 1000, x);
                    double shown = PinchZoom::frameAtX(centre, z, 1000, x);
                    double pixel = PinchZoom::framesPerPixel(z);
                    QVERIFY2(shown > frame - pixel - 0.5 &&
                             shown <= frame + 0.5,
                             qPrintable(QString("%1 at x %2, %3: shows %4")
                                        .arg(frame).arg(x).arg(text(z))
                                        .arg(shown)));
                }
            }
        }
    }
};

#endif
