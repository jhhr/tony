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

#ifndef TONY_COVERAGE_H
#define TONY_COVERAGE_H

#include "base/BaseTypes.h"

#include <vector>

/**
 * The parts of a singing take that hold recorded material, as frame
 * ranges [start, end) on the reference's timeline. The rest of the
 * take's audio file is silence. Ranges are kept sorted and apart:
 * two that overlap or touch are one range.
 */
class Coverage
{
public:
    struct Range {
        sv::sv_frame_t start;
        sv::sv_frame_t end;

        Range() : start(0), end(0) { }
        Range(sv::sv_frame_t s, sv::sv_frame_t e) : start(s), end(e) { }

        sv::sv_frame_t length() const { return end - start; }
        bool operator==(const Range &r) const {
            return start == r.start && end == r.end;
        }
    };

    typedef std::vector<Range> Ranges;

    /// Empty ranges (end <= start) are ignored by add() and remove()
    void add(sv::sv_frame_t start, sv::sv_frame_t end);
    void remove(sv::sv_frame_t start, sv::sv_frame_t end);
    void clear() { m_ranges.clear(); }

    bool isEmpty() const { return m_ranges.empty(); }
    const Ranges &getRanges() const { return m_ranges; }

    bool contains(sv::sv_frame_t frame) const;

    /// The range that contains frame, if any
    bool getRangeAt(sv::sv_frame_t frame, Range &range) const;

    bool overlaps(sv::sv_frame_t start, sv::sv_frame_t end) const;

    /// End of the last range, 0 if there is none
    sv::sv_frame_t getEndFrame() const;

    bool operator==(const Coverage &c) const { return m_ranges == c.m_ranges; }
    bool operator!=(const Coverage &c) const { return !(*this == c); }

private:
    Ranges m_ranges;
};

#endif
