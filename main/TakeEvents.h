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

#ifndef TONY_TAKE_EVENTS_H
#define TONY_TAKE_EVENTS_H

#include "Coverage.h"

#include "base/Event.h"

/**
 * The pitch events and the notes of a take, edited to follow a change
 * to its audio.  Erasing part of a take takes the singing out of its
 * audio file, and what was analysed there has to go with it.
 *
 * These are pure functions over event lists: they touch no model, so
 * they can be tested without a window, and what they return is the
 * change itself, which is what an undo command is made of.
 */
namespace TakeEvents
{
    /**
     * What a model has to lose and then gain for the change to be made.
     */
    struct Change {
        sv::EventVector removed;
        sv::EventVector added;

        bool isEmpty() const { return removed.empty() && added.empty(); }
    };

    /**
     * The pitch events in the erased ranges go; nothing else changes.
     */
    Change erasePitch(const sv::EventVector &events,
                      const Coverage::Ranges &erased);

    /**
     * The notes lose what the erased ranges take from them.  A note
     * wholly inside a range goes.  A note that a range starts inside is
     * cut back to the start of the range.  A note whose own start was
     * erased begins again at the end of the range, shorter by what was
     * taken: the singing its onset was is not there any more.  A note
     * with an erased range in the middle of it becomes two notes, as the
     * audio did.
     */
    Change eraseNotes(const sv::EventVector &events,
                      const Coverage::Ranges &erased);
}

#endif
