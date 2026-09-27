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

#include "PinchZoom.h"

#include "data/model/RelativelyFineZoomConstraint.h"

#include <cmath>

using namespace sv;

namespace PinchZoom
{

double
framesPerPixel(ZoomLevel z)
{
    if (z.zone == ZoomLevel::PixelsPerFrame) {
        return 1.0 / double(z.level);
    }
    return double(z.level);
}

ZoomLevel
nearestLevel(double fpp)
{
    // The wheel steps through View::getZoomConstraintLevel(), which is
    // this constraint alone: the layers' own constraints count only for
    // a layer that does not supportsOtherZoomLevels(), and none in svgui
    // says that. Its limits are the wheel's limits too
    RelativelyFineZoomConstraint constraint;

    double minFpp = framesPerPixel(constraint.getMinZoomLevel());
    double maxFpp = framesPerPixel(constraint.getMaxZoomLevel());
    if (!(fpp > minFpp)) fpp = minFpp; // NaN as well
    if (fpp > maxFpp) fpp = maxFpp;

    ZoomLevel requested;
    if (fpp >= 1.0) {
        requested = ZoomLevel(ZoomLevel::FramesPerPixel,
                              int(std::lround(fpp)));
    } else {
        requested = ZoomLevel(ZoomLevel::PixelsPerFrame,
                              int(std::lround(1.0 / fpp)));
    }

    return constraint.getNearestZoomLevel(requested,
                                          ZoomConstraint::RoundNearest);
}

ZoomLevel
pinchedLevel(ZoomLevel current, double targetFpp)
{
    if (!(targetFpp > 0.0)) return current;

    ZoomLevel candidate = nearestLevel(targetFpp);
    if (candidate == current) return current;

    // Nearer by 2% at least. Adjacent levels are 10% or more apart, and a
    // finger resting on a screen wanders by well under 1% of a pinch
    const double margin = std::log(1.02);

    double candidateError =
        std::fabs(std::log(framesPerPixel(candidate) / targetFpp));
    double currentError =
        std::fabs(std::log(framesPerPixel(current) / targetFpp));

    if (candidateError + margin < currentError) return candidate;
    return current;
}

double
frameAtX(sv_frame_t centre, ZoomLevel z, int width, double x)
{
    double dx = x - double(width / 2);

    if (z.zone == ZoomLevel::FramesPerPixel) {
        // View maps x from the centre rounded down to a whole pixel
        sv_frame_t rounded = (centre / z.level) * z.level;
        return double(rounded) + dx * double(z.level);
    }

    return double(centre) + dx / double(z.level);
}

sv_frame_t
centreFor(double frame, ZoomLevel z, int width, double x)
{
    double dx = x - double(width / 2);
    return sv_frame_t(std::llround(frame - dx * framesPerPixel(z)));
}

AxisMovement::AxisMovement(double deadZone) :
    m_deadZone(deadZone),
    m_counting(false),
    m_offset(0.0)
{
}

double
AxisMovement::update(double along, double across)
{
    if (!m_counting &&
        std::fabs(along) > m_deadZone &&
        2.0 * std::fabs(along) >= std::fabs(across)) {
        m_counting = true;
        m_offset = (along > 0.0 ? m_deadZone : -m_deadZone);
    }
    return m_counting ? along - m_offset : 0.0;
}

void
AxisMovement::start(double along)
{
    if (m_counting) return;
    m_counting = true;
    m_offset = along;
}

}
