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

#ifndef TEST_TAKE_EVENTS_H
#define TEST_TAKE_EVENTS_H

// Tier 2: what erasing part of a take does to the pitch events and the
// notes that were analysed there. Event lists in, changes out; no
// models and no window.

#include "../TakeEvents.h"

#include <QObject>
#include <QtTest>

class TestTakeEvents : public QObject
{
    Q_OBJECT

    typedef sv::sv_frame_t frame_t;
    typedef Coverage::Range Range;
    typedef Coverage::Ranges Ranges;

    // A pitch event: a frequency at a frame, no duration
    static sv::Event pitchAt(frame_t frame, float hz = 220.f) {
        return sv::Event(frame, hz, QString());
    }

    static sv::Event noteAt(frame_t frame, frame_t duration,
                            float hz = 220.f) {
        return sv::Event(frame, hz, duration, "sung");
    }

    static sv::EventVector pitchTrack(frame_t from, frame_t to,
                                      frame_t step) {
        sv::EventVector events;
        for (frame_t f = from; f < to; f += step) events.push_back(pitchAt(f));
        return events;
    }

private slots:
    // The pitch events in the erased ranges go, and nothing is added in
    // their place: there is nothing there to hear any more
    void pitch_in_the_erased_ranges_goes() {
        sv::EventVector events = pitchTrack(0, 1000, 100);
        TakeEvents::Change change =
            TakeEvents::erasePitch(events, Ranges { Range(300, 600) });

        QVERIFY(change.added.empty());
        QCOMPARE(int(change.removed.size()), 3);
        QCOMPARE(change.removed[0].getFrame(), frame_t(300));
        QCOMPARE(change.removed[1].getFrame(), frame_t(400));
        QCOMPARE(change.removed[2].getFrame(), frame_t(500));
    }

    // A range is [start, end): an event at the end of it is outside
    void pitch_at_the_edges_of_a_range() {
        sv::EventVector events { pitchAt(299), pitchAt(300), pitchAt(599),
                                 pitchAt(600) };
        TakeEvents::Change change =
            TakeEvents::erasePitch(events, Ranges { Range(300, 600) });

        QCOMPARE(int(change.removed.size()), 2);
        QCOMPARE(change.removed[0].getFrame(), frame_t(300));
        QCOMPARE(change.removed[1].getFrame(), frame_t(599));
    }

    void pitch_in_several_ranges() {
        sv::EventVector events = pitchTrack(0, 1000, 100);
        // Out of order, and two of them overlapping
        TakeEvents::Change change = TakeEvents::erasePitch
            (events, Ranges { Range(800, 900), Range(100, 250),
                              Range(200, 300) });

        QCOMPARE(int(change.removed.size()), 3);
        QCOMPARE(change.removed[0].getFrame(), frame_t(100));
        QCOMPARE(change.removed[1].getFrame(), frame_t(200));
        QCOMPARE(change.removed[2].getFrame(), frame_t(800));
    }

    void nothing_erased_changes_nothing() {
        sv::EventVector events = pitchTrack(0, 1000, 100);
        QVERIFY(TakeEvents::erasePitch(events, Ranges {}).isEmpty());
        QVERIFY(TakeEvents::eraseNotes
                (sv::EventVector { noteAt(0, 1000) }, Ranges {}).isEmpty());

        // A range that does not reach the events
        QVERIFY(TakeEvents::erasePitch
                (events, Ranges { Range(2000, 3000) }).isEmpty());
        QVERIFY(TakeEvents::eraseNotes
                (sv::EventVector { noteAt(0, 1000) },
                 Ranges { Range(2000, 3000) }).isEmpty());
    }

    // A note the erased range takes in altogether goes
    void note_wholly_inside_goes() {
        sv::EventVector notes { noteAt(400, 100) };
        TakeEvents::Change change =
            TakeEvents::eraseNotes(notes, Ranges { Range(300, 600) });

        QCOMPARE(int(change.removed.size()), 1);
        QCOMPARE(change.removed[0].getFrame(), frame_t(400));
        QVERIFY(change.added.empty());

        // and one that fills it exactly
        change = TakeEvents::eraseNotes(sv::EventVector { noteAt(300, 300) },
                                        Ranges { Range(300, 600) });
        QCOMPARE(int(change.removed.size()), 1);
        QVERIFY(change.added.empty());
    }

    // A note that runs into the range from before it keeps its onset and
    // is cut back to where the erased audio begins
    void note_cut_back_at_the_start_of_a_range() {
        sv::EventVector notes { noteAt(100, 300, 330.f) };
        TakeEvents::Change change =
            TakeEvents::eraseNotes(notes, Ranges { Range(300, 600) });

        QCOMPARE(int(change.removed.size()), 1);
        QCOMPARE(int(change.added.size()), 1);
        QCOMPARE(change.added[0].getFrame(), frame_t(100));
        QCOMPARE(change.added[0].getDuration(), frame_t(200));
        // and it is the same note otherwise
        QCOMPARE(change.added[0].getValue(), 330.f);
        QCOMPARE(change.added[0].getLabel(), QString("sung"));
    }

    // A note whose own start was erased has lost the singing its onset
    // was: it begins again where the audio does, and is shorter by what
    // was taken from it
    void note_moved_on_from_the_end_of_a_range() {
        sv::EventVector notes { noteAt(400, 500) };
        TakeEvents::Change change =
            TakeEvents::eraseNotes(notes, Ranges { Range(300, 600) });

        QCOMPARE(int(change.removed.size()), 1);
        QCOMPARE(int(change.added.size()), 1);
        QCOMPARE(change.added[0].getFrame(), frame_t(600));
        QCOMPARE(change.added[0].getDuration(), frame_t(300));
    }

    // A range erased from the middle of a note leaves two notes, as it
    // leaves two stretches of audio
    void note_split_by_a_range_in_the_middle() {
        sv::EventVector notes { noteAt(100, 800) };
        TakeEvents::Change change =
            TakeEvents::eraseNotes(notes, Ranges { Range(300, 600) });

        QCOMPARE(int(change.removed.size()), 1);
        QCOMPARE(int(change.added.size()), 2);
        QCOMPARE(change.added[0].getFrame(), frame_t(100));
        QCOMPARE(change.added[0].getDuration(), frame_t(200));
        QCOMPARE(change.added[1].getFrame(), frame_t(600));
        QCOMPARE(change.added[1].getDuration(), frame_t(300));
    }

    // Several ranges through one note, given out of order
    void note_through_several_ranges() {
        sv::EventVector notes { noteAt(0, 1000) };
        TakeEvents::Change change = TakeEvents::eraseNotes
            (notes, Ranges { Range(600, 700), Range(250, 400),
                             Range(200, 300) });

        QCOMPARE(int(change.removed.size()), 1);
        QCOMPARE(int(change.added.size()), 3);
        QCOMPARE(change.added[0].getFrame(), frame_t(0));
        QCOMPARE(change.added[0].getDuration(), frame_t(200));
        QCOMPARE(change.added[1].getFrame(), frame_t(400));
        QCOMPARE(change.added[1].getDuration(), frame_t(200));
        QCOMPARE(change.added[2].getFrame(), frame_t(700));
        QCOMPARE(change.added[2].getDuration(), frame_t(300));
    }

    // Only the notes the ranges reach are touched
    void notes_outside_are_left_alone() {
        sv::EventVector notes { noteAt(0, 100), noteAt(400, 100),
                                noteAt(900, 100) };
        TakeEvents::Change change =
            TakeEvents::eraseNotes(notes, Ranges { Range(300, 600) });

        QCOMPARE(int(change.removed.size()), 1);
        QCOMPARE(change.removed[0].getFrame(), frame_t(400));
        QVERIFY(change.added.empty());
    }

    // A note that ends where the range starts, and one that starts where
    // it ends, are outside it
    void notes_at_the_edges_of_a_range() {
        sv::EventVector notes { noteAt(200, 100), noteAt(600, 100) };
        QVERIFY(TakeEvents::eraseNotes
                (notes, Ranges { Range(300, 600) }).isEmpty());
    }

    // A note of no length: nothing pYIN makes, but the rule is the same
    // as for a pitch event
    void a_note_of_no_length() {
        sv::EventVector notes { noteAt(400, 0), noteAt(700, 0) };
        TakeEvents::Change change =
            TakeEvents::eraseNotes(notes, Ranges { Range(300, 600) });

        QCOMPARE(int(change.removed.size()), 1);
        QCOMPARE(change.removed[0].getFrame(), frame_t(400));
        QVERIFY(change.added.empty());
    }
};

#endif
