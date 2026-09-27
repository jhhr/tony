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

#include "OctaveSlips.h"

#include <cmath>

using sv::sv_frame_t;

namespace {

double
centsBetween(double fromHz, double hz)
{
    return 1200.0 * std::log2(hz / fromHz);
}

} // namespace

int
OctaveSlips::octaveFrom(double fromHz, double hz)
{
    double cents = centsBetween(fromHz, hz);
    if (std::fabs(cents - 1200.0) <= kToleranceCents) return 1;
    if (std::fabs(cents + 1200.0) <= kToleranceCents) return -1;
    return 0;
}

void
OctaveSlips::letThrough(const Dot &dot, std::vector<Dot> &out)
{
    // Unless it is an octave from the dot before it, which may be a slip
    if (m_haveLast) {
        int direction = octaveFrom(m_last.hz, dot.hz);
        if (direction != 0) {
            m_held.push_back(dot);
            m_heldDirection = direction;
            return;
        }
    }
    out.push_back(dot);
    m_last = dot;
    m_haveLast = true;
}

void
OctaveSlips::releaseHeld(std::vector<Dot> &out)
{
    if (m_held.empty()) return;
    out.insert(out.end(), m_held.begin(), m_held.end());
    m_last = m_held.back();
    m_held.clear();
    m_heldDirection = 0;
}

void
OctaveSlips::push(sv_frame_t frame, double hz, std::vector<Dot> &out)
{
    // A run that ends in silence cannot be told from a phrase that ends
    // an octave up or down: it is let through
    if (!(hz > 0.0)) {
        releaseHeld(out);
        m_haveLast = false;
        return;
    }

    Dot dot { frame, hz };

    if (m_held.empty()) {
        letThrough(dot, out);
        return;
    }

    // Back at the pitch before the run: the run was a slip
    if (std::fabs(centsBetween(m_last.hz, hz)) <= kToleranceCents) {
        m_held.clear();
        m_heldDirection = 0;
        out.push_back(dot);
        m_last = dot;
        return;
    }

    // Still an octave away, and not for longer than a slip lasts
    if (octaveFrom(m_last.hz, hz) == m_heldDirection &&
        int(m_held.size()) < kMaxRunHops) {
        m_held.push_back(dot);
        return;
    }

    // The singing went there: the run is let through, and this dot
    // follows it as any dot follows the one before
    releaseHeld(out);
    letThrough(dot, out);
}
