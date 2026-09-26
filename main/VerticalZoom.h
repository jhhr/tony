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

#ifndef TONY_VERTICAL_ZOOM_H
#define TONY_VERTICAL_ZOOM_H

/**
 * The arithmetic of two fingers on a pane's vertical axis: the value
 * at a height in the pane, the range that zooms about a value held at
 * a height, and the limits a range is kept within. TouchGestures does
 * as the answers say.
 *
 * The mapping is svgui's CoordinateScale's for a vertical scale: y
 * from the top of the pane, the range's minimum at the bottom edge (y
 * is the height) and its maximum at the top (y is 0), the values
 * between spaced evenly on a linear scale and by ratio on a
 * logarithmic one. Zooming and scrolling happen on that mapped axis,
 * so that on a log scale an octave keeps its height wherever it is.
 *
 * Nothing here touches a view, so all of it is tested without a
 * window (TestVerticalZoom).
 */
namespace VerticalZoom
{
    /// The values at the bottom and the top of a pane, and the scale
    struct Range {
        double min = 0.0;
        double max = 0.0;
        bool log = false;
    };

    inline bool operator==(const Range &a, const Range &b) {
        return a.min == b.min && a.max == b.max && a.log == b.log;
    }
    inline bool operator!=(const Range &a, const Range &b) {
        return !(a == b);
    }

    /// How far a range may go
    struct Limits {
        double lowest = 0.0;    // its min no lower than this
        double highest = 0.0;   // its max no higher
        double narrowest = 1.0; // its max at least this many times its min
    };

    /**
     * For a pitch track: from A0 to C8, the piano's range, which holds
     * any note that is sung and the default range with room to spare;
     * and four semitones at the narrowest.
     */
    Limits pitchLimits();

    /// The value at y in a pane of the given height showing range
    double valueAtY(const Range &range, double height, double y);

    /// Where value is in a pane of the given height showing range
    double yForValue(const Range &range, double height, double value);

    /**
     * The range factor times narrower than range, on its own scale,
     * that shows value at y: above 1 zooms in, below 1 out. With a
     * factor of 1 it is range scrolled, whatever it had at y.
     */
    Range zoomedAbout(const Range &range, double factor, double value,
                      double height, double y);

    /**
     * range within limits: widened about y, if narrower than they
     * allow; then moved inside them, or made all of them if wider.
     * A range within the limits comes back as it is. One that shows
     * nothing (no width, or a log scale from zero) becomes all of them.
     */
    Range limited(const Range &range, const Limits &limits,
                  double height, double y);
}

#endif
