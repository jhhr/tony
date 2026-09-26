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

#ifndef TONY_SONG_SCROLL_H
#define TONY_SONG_SCROLL_H

#include "base/BaseTypes.h"

#include <utility>
#include <vector>

/**
 * The arithmetic of the song scroll bar (SongScrollBar): a strip that
 * shows the whole song, frame 0 at its left edge and the song's end at
 * its right, with a thumb for what the panes show. Where a frame is on
 * the strip and back, the thumb's geometry, where a drag or a press
 * puts the panes' centre, and the contour of the reference's pitch
 * drawn along it.
 *
 * Positions are in pixels from the strip's left edge, and may be
 * fractional. Nothing here touches a widget, so all of it is tested
 * without a window (TestSongScroll).
 */
namespace SongScroll
{
    /// The x of frame on a strip width pixels wide showing songFrames
    double xForFrame(sv::sv_frame_t frame, sv::sv_frame_t songFrames,
                     double width);

    /// The frame at x: xForFrame()'s inverse, rounded to a frame
    sv::sv_frame_t frameForX(double x, sv::sv_frame_t songFrames,
                             double width);

    /// frame, or the song's first or last frame if it is outside it
    sv::sv_frame_t clampToSong(sv::sv_frame_t frame,
                               sv::sv_frame_t songFrames);

    struct Thumb {
        double x0 = 0.0;
        double x1 = 0.0;
        double width() const { return x1 - x0; }
    };

    /**
     * The thumb for panes that show the frames start to end: the part of
     * the strip they cover, cut to the strip, then at least minWidth
     * wide (or the strip's width if that is less) and moved back within
     * the strip if that took it over an edge. The panes may show more
     * than the song, and then the thumb is the whole strip.
     */
    Thumb thumb(sv::sv_frame_t start, sv::sv_frame_t end,
                sv::sv_frame_t songFrames, double width, double minWidth);

    /// Whether a press at x is on the thumb, its edges included
    bool hitsThumb(const Thumb &thumb, double x);

    /**
     * The centre a drag asks for: the panes' centre when the strip was
     * grabbed at grabX, moved by as many frames as the strip shows in
     * the distance from grabX to x, and kept within the song.
     */
    sv::sv_frame_t dragCentre(sv::sv_frame_t grabbedCentre, double grabX,
                              double x, sv::sv_frame_t songFrames,
                              double width);

    /// The centre a press away from the thumb asks for: the frame there
    sv::sv_frame_t jumpCentre(double x, sv::sv_frame_t songFrames,
                              double width);

    /// The pitch in one column of the strip, in Hz: none if high is 0
    struct Column {
        double low = 0.0;
        double high = 0.0;
        bool isEmpty() const { return !(high > 0.0); }
    };

    /**
     * For each of the given number of columns across the strip, the
     * lowest and highest of the pitches in the frames it shows. An event
     * (frame, value) stands for the resolution frames from its own, as
     * a pitch track's does, so that a column narrower than that is not
     * left empty between two events. A value of 0 or less is no pitch.
     * A column with no pitch in it, as where the pitch track has no
     * events, is empty.
     */
    std::vector<Column> pitchColumns
    (const std::vector<std::pair<sv::sv_frame_t, double>> &events,
     sv::sv_frame_t resolution, sv::sv_frame_t songFrames, int columns);

    /// The lowest and highest pitch in the columns; false if all empty
    bool pitchRange(const std::vector<Column> &columns,
                    double &low, double &high);

    /**
     * Where hz goes on a log scale from low at height down to high at
     * 0, for a band height pixels high: the middle if the range is a
     * single pitch.
     */
    double yForPitch(double hz, double low, double high, double height);
}

#endif
