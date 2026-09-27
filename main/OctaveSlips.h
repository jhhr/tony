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

#ifndef TONY_OCTAVE_SLIPS_H
#define TONY_OCTAVE_SLIPS_H

#include "base/BaseTypes.h"

#include <vector>

/**
 * Drops the live tracker's octave slips: a short run of dots an octave
 * from the dots on both sides of it.
 *
 * YIN takes the first lag whose normalised difference is under its
 * threshold. Where noise lifts the dip at the period just over it, the
 * dip at twice the period, normalised by a larger mean, can still be
 * under: the dot comes out an octave low (and, the other way, an octave
 * high). On the user's phone, with the input at -28 dBFS, 4 of about
 * 280 dots a punch-in slipped so, which the dev checks' item 3 failed
 * on. pYIN, whose pitch replaces the dots once a take is analysed, does
 * not slip so.
 *
 * Fed every hop the tracker analyses, in order, voiced or not. A dot
 * about an octave from the dot on the hop before it is held back, with
 * those after it that are too, until one comes back to the pitch before
 * the run: then the run is dropped. A run that goes on longer than one
 * window's hops, or ends in an unvoiced hop or at another pitch, is let
 * through, late by as long as it was held: a leap of an octave in the
 * singing. A dot with no dot on the hop before it (the start of a
 * phrase) is let through as it is. So only dots after an octave's jump
 * are ever late, by one window at the most.
 *
 * Plain arithmetic, for the tracker's thread; tested in TestOctaveSlips.
 */
class OctaveSlips
{
public:
    struct Dot {
        sv::sv_frame_t frame;
        double hz;
    };

    /// The longest run dropped, in hops: one window of the tracker's
    /// (2048 frames in hops of 256), as a slip comes of what is in one
    /// window, and every window that holds it may slip
    static constexpr int kMaxRunHops = 8;

    /// How near an octave a dot has to be to the one before the run,
    /// and how near its pitch the dot after the run, in cents
    static constexpr double kToleranceCents = 100.0;

    /**
     * The next hop, at frame: its pitch, or 0 for an unvoiced hop.
     * Appends to out, oldest first, the dots now let through.
     */
    void push(sv::sv_frame_t frame, double hz, std::vector<Dot> &out);

    /// How many dots are held back now
    int held() const { return int(m_held.size()); }

private:
    /// +1 for about an octave above hz, -1 below, else 0
    static int octaveFrom(double fromHz, double hz);
    void letThrough(const Dot &dot, std::vector<Dot> &out);
    void releaseHeld(std::vector<Dot> &out);

    bool m_haveLast = false;
    Dot m_last { 0, 0.0 };      ///< the dot let through on the hop
                                ///< before the held run, if voiced
    std::vector<Dot> m_held;
    int m_heldDirection = 0;
};

#endif
