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

#ifndef TONY_PINCH_ZOOM_H
#define TONY_PINCH_ZOOM_H

#include "base/BaseTypes.h"
#include "base/ZoomLevel.h"

/**
 * The arithmetic of a two-finger gesture on a pane's time axis: which
 * zoom level a pinch asks for, and where the pane must be centred for
 * a frame to stay under the fingers; and, for either axis, when the
 * fingers' movement along it counts.  TouchGestures does as the
 * answers say (VerticalZoom has the vertical axis).
 *
 * The x mapping is svgui's View::getFrameForX() made continuous: the
 * same centre, rounded the same way, so that a frame placed at x by
 * centreFor() is the one the pane shows there, to within a pixel.
 *
 * Nothing here touches a view, so all of it is tested without a
 * window (TestPinchZoom).
 */
namespace PinchZoom
{
    /// Frames per pixel at z: below 1 when zoomed in past one to one
    double framesPerPixel(sv::ZoomLevel z);

    /**
     * The zoom level nearest to fpp frames per pixel among those the
     * mouse wheel steps through, and within the same limits.
     */
    sv::ZoomLevel nearestLevel(double fpp);

    /**
     * The level for a pinch that asks for targetFpp, with the view at
     * current: the nearest level, but current until another is clearly
     * nearer, so that fingers held still half way between two levels
     * do not make the view flicker between them.
     */
    sv::ZoomLevel pinchedLevel(sv::ZoomLevel current, double targetFpp);

    /**
     * The frame at x (pixels, may be fractional) in a view of the given
     * width centred on centre at zoom z.
     */
    double frameAtX(sv::sv_frame_t centre, sv::ZoomLevel z, int width,
                    double x);

    /**
     * The centre that puts frame at x in a view of the given width at
     * zoom z.
     */
    sv::sv_frame_t centreFor(double frame, sv::ZoomLevel z, int width,
                             double x);

    /**
     * How much of the fingers' movement along one axis counts: the
     * change in their spread, or the travel of the point between them,
     * since they came down. None until the movement is plainly meant:
     * more than the dead zone, and at least half as much as the same
     * movement across the axis, so that a pinch or a drag along one
     * axis leaves the other alone. From then on all of it, less what
     * was taken for the dead zone, so that nothing jumps when it
     * starts to count.
     */
    class AxisMovement
    {
    public:
        explicit AxisMovement(double deadZone = 0.0);

        /// What counts of the movement along, with across the other's
        double update(double along, double across);

        /// Count from now on, from along as it is now
        void start(double along);

        bool isCounting() const { return m_counting; }

    private:
        double m_deadZone;
        bool m_counting;
        double m_offset;
    };
}

#endif
