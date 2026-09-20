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

#include "TakeEvents.h"

using namespace sv;

namespace {

// In order and apart, so that one pass over them is enough
Coverage::Ranges tidied(const Coverage::Ranges &ranges)
{
    Coverage coverage;
    for (const Coverage::Range &r : ranges) coverage.add(r.start, r.end);
    return coverage.getRanges();
}

bool covers(const Coverage::Ranges &ranges, sv_frame_t frame)
{
    for (const Coverage::Range &r : ranges) {
        if (frame >= r.start && frame < r.end) return true;
    }
    return false;
}

} // namespace

TakeEvents::Change
TakeEvents::erasePitch(const EventVector &events,
                       const Coverage::Ranges &erased)
{
    Coverage::Ranges ranges = tidied(erased);

    Change change;
    for (const Event &e : events) {
        if (covers(ranges, e.getFrame())) change.removed.push_back(e);
    }
    return change;
}

TakeEvents::Change
TakeEvents::eraseNotes(const EventVector &events,
                       const Coverage::Ranges &erased)
{
    Coverage::Ranges ranges = tidied(erased);

    Change change;

    for (const Event &e : events) {

        sv_frame_t start = e.getFrame();
        sv_frame_t end = start + e.getDuration();

        // A note with no length is either in an erased range or not
        if (end <= start) {
            if (covers(ranges, start)) change.removed.push_back(e);
            continue;
        }

        // What is left of the note: the erased ranges taken out of it.
        // Two stretches can be left, when a range was erased from the
        // middle of the note
        Coverage::Ranges left;
        sv_frame_t from = start;

        for (const Coverage::Range &r : ranges) {
            if (r.end <= from) continue;
            if (r.start >= end) break;
            if (r.start > from) {
                left.push_back(Coverage::Range(from, r.start));
            }
            from = r.end;
            if (from >= end) break;
        }
        if (from < end) left.push_back(Coverage::Range(from, end));

        // Nothing of this one was erased
        if (left.size() == 1 &&
            left[0].start == start && left[0].end == end) {
            continue;
        }

        change.removed.push_back(e);

        for (const Coverage::Range &r : left) {
            change.added.push_back
                (e.withFrame(r.start).withDuration(r.length()));
        }
    }

    return change;
}
