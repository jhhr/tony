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

#ifndef TEST_PITCH_CANDIDATES_H
#define TEST_PITCH_CANDIDATES_H

// Tier 2: how a selection's pitch candidates lie by pitch, and which
// one a step up or down from the pitch track goes to. Tracks in, an
// index out; no analysis, no layers.

#include "../PitchCandidates.h"

#include <QObject>
#include <QtTest>

#include <cmath>
#include <vector>

class TestPitchCandidates : public QObject
{
    Q_OBJECT

    typedef PitchCandidates::Track Track;

    static constexpr sv::sv_frame_t hop = 256;

    // Hops from..to (not including to) at hz, after what is in t
    static Track line(Track t, int from, int to, double hz) {
        for (int i = from; i < to; ++i) t.push_back({ i * hop, hz });
        return t;
    }
    static Track line(int from, int to, double hz) {
        return line(Track(), from, to, hz);
    }

    // A harmony's voices over 100 hops: a third and a fifth above 220 Hz
    static double low() { return 220.0; }
    static double mid() { return 220.0 * std::pow(2.0, 4 / 12.0); }
    static double top() { return 220.0 * std::pow(2.0, 7 / 12.0); }

private slots:

    void a_third_above_lies_above() {
        QCOMPARE(PitchCandidates::compare(line(0, 100, mid()), line(0, 100, low())), 1);
        QCOMPARE(PitchCandidates::compare(line(0, 100, low()), line(0, 100, mid())), -1);
    }

    void within_a_quarter_tone_is_the_same() {
        double slightlyOff = low() * std::pow(2.0, 40 / 1200.0);
        QCOMPARE(PitchCandidates::compare(line(0, 100, slightlyOff), line(0, 100, low())), 0);
    }

    void a_few_frames_apart_is_the_same_line() {
        // 5 of 100 frames on the other voice: under a tenth
        Track a = line(line(0, 95, low()), 95, 100, mid());
        QCOMPARE(PitchCandidates::compare(a, line(0, 100, low())), 0);
        // 15 of 100 frames: over a tenth, and above on all of them
        Track b = line(line(0, 85, low()), 85, 100, mid());
        QCOMPARE(PitchCandidates::compare(b, line(0, 100, low())), 1);
    }

    void compared_only_where_both_are_voiced() {
        // Above the lower voice on the frames they share, though its
        // median over all of its own frames is lower
        Track a = line(line(0, 50, mid()), 50, 150, 110.0);
        Track b = line(0, 50, low());
        QCOMPARE(PitchCandidates::compare(a, b), 1);
    }

    void tracks_with_nothing_in_common_go_by_median() {
        QCOMPARE(PitchCandidates::compare(line(0, 50, mid()), line(50, 100, low())), 1);
        QCOMPARE(PitchCandidates::compare(line(0, 50, low()), line(50, 100, low())), 0);
        QCOMPARE(PitchCandidates::compare(Track(), line(0, 100, low())), 0);
    }

    void frames_matched_within_half_a_hop() {
        Track a;
        for (int i = 0; i < 100; ++i) a.push_back({ i * hop + 100, mid() });
        QCOMPARE(PitchCandidates::compare(a, line(0, 100, low())), 1);
    }

    void the_same_line() {
        Track a = line(0, 100, low());
        QVERIFY(PitchCandidates::same(a, a));
        // Within a quarter tone, and matched within half a hop
        Track slightlyOff;
        for (int i = 0; i < 100; ++i) {
            slightlyOff.push_back({ i * hop + 100, low() * std::pow(2.0, 40 / 1200.0) });
        }
        QVERIFY(PitchCandidates::same(a, slightlyOff));
        // On the other voice in 5 frames of 100, or voiced in 5 more
        QVERIFY(PitchCandidates::same(a, line(line(0, 95, low()), 95, 100, mid())));
        QVERIFY(PitchCandidates::same(a, line(0, 105, low())));
    }

    void not_the_same_line() {
        Track a = line(0, 100, low());
        QVERIFY(!PitchCandidates::same(a, line(0, 100, mid())));
        // On the other voice in 15 frames of 100, or voiced in half
        QVERIFY(!PitchCandidates::same(a, line(line(0, 85, low()), 85, 100, mid())));
        QVERIFY(!PitchCandidates::same(a, line(0, 50, low())));
        QVERIFY(!PitchCandidates::same(a, Track()));
        // Lines that cross, as far below as above
        Track crossing = line(line(0, 50, mid()), 50, 100, 220.0 * std::pow(2.0, -4 / 12.0));
        QCOMPARE(PitchCandidates::compare(crossing, a), 0);
        QVERIFY(!PitchCandidates::same(crossing, a));
    }

    void distinct_keeps_the_first_of_each_line() {
        std::vector<Track> c { line(0, 100, low()), Track(), line(0, 100, mid()),
                               line(line(0, 97, low()), 97, 100, mid()),
                               line(0, 100, mid()) };
        QCOMPARE(PitchCandidates::distinct(c), (std::vector<int>{ 0, 2 }));
    }

    void ordered_low_to_high() {
        std::vector<Track> c { line(0, 100, top()), Track(),
                               line(0, 100, low()), line(0, 100, mid()) };
        QCOMPARE(PitchCandidates::order(c), (std::vector<int>{ 2, 3, 0, 1 }));
    }

    void step_from_the_lower_voice() {
        std::vector<Track> c { line(0, 100, low()), line(0, 100, mid()) };
        Track current = line(0, 100, low());
        QCOMPARE(PitchCandidates::step(c, current, true), 1);
        QCOMPARE(PitchCandidates::step(c, current, false), -1);
    }

    void step_from_the_middle_of_three() {
        // Given highest first, as nothing promises an order
        std::vector<Track> c { line(0, 100, top()), line(0, 100, mid()),
                               line(0, 100, low()) };
        Track current = line(0, 100, mid());
        QCOMPARE(PitchCandidates::step(c, current, true), 0);
        QCOMPARE(PitchCandidates::step(c, current, false), 2);
    }

    void step_from_a_track_that_jumps_between_voices() {
        // The case Choose Higher Pitch is for: the track takes the
        // lower voice, then the upper, then the lower again
        std::vector<Track> c { line(0, 100, low()), line(0, 100, mid()) };
        Track current = line(line(line(0, 30, low()), 30, 70, mid()), 70, 100, low());
        QCOMPARE(PitchCandidates::step(c, current, true), 1);
        QCOMPARE(PitchCandidates::step(c, current, false), 0);
    }

    void step_from_an_empty_track() {
        std::vector<Track> c { line(0, 100, mid()), Track(), line(0, 100, low()) };
        QCOMPARE(PitchCandidates::step(c, Track(), true), 2);
        QCOMPARE(PitchCandidates::step(c, Track(), false), 0);
        QCOMPARE(PitchCandidates::step({ Track() }, Track(), true), -1);
    }
};

#endif
