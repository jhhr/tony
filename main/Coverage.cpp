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

#include "Coverage.h"

#include <algorithm>

using namespace sv;

void
Coverage::add(sv_frame_t start, sv_frame_t end)
{
    if (end <= start) return;

    Ranges result;
    bool placed = false;

    for (const Range &r : m_ranges) {
        if (r.end < start) {
            result.push_back(r);
        } else if (r.start > end) {
            if (!placed) {
                result.push_back(Range(start, end));
                placed = true;
            }
            result.push_back(r);
        } else {
            // overlaps or touches: becomes part of the new one
            start = std::min(start, r.start);
            end = std::max(end, r.end);
        }
    }

    if (!placed) result.push_back(Range(start, end));

    m_ranges = result;
}

void
Coverage::remove(sv_frame_t start, sv_frame_t end)
{
    if (end <= start) return;

    Ranges result;

    for (const Range &r : m_ranges) {
        if (r.end <= start || r.start >= end) {
            result.push_back(r);
            continue;
        }
        if (r.start < start) result.push_back(Range(r.start, start));
        if (r.end > end) result.push_back(Range(end, r.end));
    }

    m_ranges = result;
}

bool
Coverage::contains(sv_frame_t frame) const
{
    Range r;
    return getRangeAt(frame, r);
}

bool
Coverage::getRangeAt(sv_frame_t frame, Range &range) const
{
    for (const Range &r : m_ranges) {
        if (frame >= r.start && frame < r.end) {
            range = r;
            return true;
        }
    }
    return false;
}

bool
Coverage::overlaps(sv_frame_t start, sv_frame_t end) const
{
    if (end <= start) return false;
    for (const Range &r : m_ranges) {
        if (r.start < end && r.end > start) return true;
    }
    return false;
}

sv_frame_t
Coverage::getEndFrame() const
{
    if (m_ranges.empty()) return 0;
    return m_ranges.back().end;
}
