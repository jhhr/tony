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

#ifndef TONY_PITCH_CANDIDATES_H
#define TONY_PITCH_CANDIDATES_H

#include "base/BaseTypes.h"

#include <utility>
#include <vector>

/**
 * How a selection's pitch candidates lie against each other and against
 * the pitch track, so that Choose Higher Pitch and Choose Lower Pitch
 * step between them by pitch, and Prefer Lower Voice and Prefer Higher
 * Voice find the lowest and the highest.
 *
 * A candidate is a whole pitch track over the selection: an analysis of
 * it that follows one line (in a harmony, one voice), voiced where that
 * analysis heard singing. Two tracks are compared on the frames where
 * both are voiced. One lies above the other if it is more than a
 * quarter tone higher on more of those frames than it is lower, and on
 * at least a tenth of them: a track that differs from another in a few
 * frames only is the same line. Tracks with no voiced frame in common
 * are compared by their median pitch.
 *
 * A step goes from what the pitch track holds now, not from the last
 * candidate chosen: the user may have edited the track since, and from
 * a track that jumps between two voices, the next one up is the upper
 * voice and the next one down the lower.
 *
 * Plain arithmetic on (frame, Hz) pairs; tested in TestPitchCandidates.
 */
namespace PitchCandidates
{
    /// One track over a selection: its voiced frames only, as (frame,
    /// Hz), in frame order
    typedef std::vector<std::pair<sv::sv_frame_t, double>> Track;

    /// How far apart two pitches are before one is above the other
    constexpr double kSameCents = 50.0;

    /// The share of the frames two tracks have in common on which one
    /// has to be above the other for it to lie above as a whole
    constexpr double kMinShare = 0.1;

    /// +1 if a lies above b, -1 if below, 0 if neither (the same line,
    /// lines that cross as much one way as the other, or either has no
    /// voiced frame)
    int compare(const Track &a, const Track &b);

    /// The share of the frames voiced in either track on which both
    /// have to be voiced and within kSameCents for them to be the same
    constexpr double kSameShare = 0.9;

    /// Whether two tracks are the same line: two analyses that found the
    /// same pitches where either found any
    bool same(const Track &a, const Track &b);

    /// The candidates worth offering, in the order given: the first of
    /// each that are the same line, and none with no voiced frame
    std::vector<int> distinct(const std::vector<Track> &candidates);

    /// The candidates' indices from the lowest to the highest
    std::vector<int> order(const std::vector<Track> &candidates);

    /// The candidate to step to from the current track: the lowest of
    /// those above it (up), or the highest of those below it (down).
    /// From a track with no voiced frame, the lowest candidate going up
    /// and the highest going down. -1 if there is none to step to
    int step(const std::vector<Track> &candidates, const Track &current,
             bool up);
}

#endif
