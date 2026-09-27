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

#ifndef TEST_OCTAVE_SLIPS_H
#define TEST_OCTAVE_SLIPS_H

// Tier 2: the live dots' octave slips. Hops in, the dots let through
// out; no tracker, no audio.

#include "../OctaveSlips.h"

#include <QObject>
#include <QtTest>

#include <cmath>
#include <vector>

class TestOctaveSlips : public QObject
{
    Q_OBJECT

    typedef std::vector<OctaveSlips::Dot> Dots;

    static constexpr sv::sv_frame_t hop = 256;
    static constexpr double tone = 220.5;

    // Pushes each pitch as the next hop (0 for an unvoiced one), and
    // gives what was let through, and after which hop each came
    struct Run {
        OctaveSlips slips;
        Dots out;
        std::vector<int> after;
        int hops = 0;

        void push(double hz) {
            Dots now;
            slips.push(hops * hop, hz, now);
            for (const OctaveSlips::Dot &d : now) {
                out.push_back(d);
                after.push_back(hops);
            }
            ++hops;
        }
        void push(double hz, int count) {
            for (int i = 0; i < count; ++i) push(hz);
        }
        std::vector<int> hopsOut() const {
            std::vector<int> h;
            for (const OctaveSlips::Dot &d : out) h.push_back(int(d.frame / hop));
            return h;
        }
    };

    static std::vector<int> range(int from, int to) {
        std::vector<int> r;
        for (int i = from; i < to; ++i) r.push_back(i);
        return r;
    }
    static std::vector<int> join(std::vector<int> a, const std::vector<int> &b) {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }

private slots:

    // A steady tone, with a vibrato and a glide of a fifth: every dot
    // at once, on the hop it was found on
    void steady_dots_pass_at_once() {
        Run r;
        for (int i = 0; i < 20; ++i) {
            r.push(tone * std::pow(2.0, (i % 2 ? 10.0 : -10.0) / 1200.0));
        }
        r.push(tone * 1.5, 5);
        QCOMPARE(r.hopsOut(), range(0, 25));
        for (int i = 0; i < 25; ++i) QCOMPARE(r.after[i], i);
        QCOMPARE(r.slips.held(), 0);
        QCOMPARE(r.out[22].hz, tone * 1.5);
    }

    // One dot an octave low between two on the tone: dropped, and the
    // dot after it comes at once
    void one_dot_an_octave_low_is_dropped() {
        Run r;
        r.push(tone, 5);
        r.push(tone / 2.0 * std::pow(2.0, 9.0 / 1200.0)); // -1191 cents
        r.push(tone, 5);
        QCOMPARE(r.hopsOut(), join(range(0, 5), range(6, 11)));
        QCOMPARE(r.after[5], 6);
        for (const OctaveSlips::Dot &d : r.out) QCOMPARE(d.hz, tone);
    }

    // So is a run an octave high
    void a_run_an_octave_high_is_dropped() {
        Run r;
        r.push(tone, 3);
        r.push(tone * 2.0, 3);
        r.push(tone, 3);
        QCOMPARE(r.hopsOut(), join(range(0, 3), range(6, 9)));
    }

    // A run of one window's hops is dropped; one hop longer is taken
    // for the singing, let through whole and in order, and what follows
    // at once
    void a_run_longer_than_a_window_is_let_through() {
        {
            Run r;
            r.push(tone, 3);
            r.push(tone / 2.0, OctaveSlips::kMaxRunHops);
            r.push(tone, 3);
            QCOMPARE(r.hopsOut(),
                     join(range(0, 3),
                          range(3 + OctaveSlips::kMaxRunHops,
                                6 + OctaveSlips::kMaxRunHops)));
        }
        {
            Run r;
            r.push(tone, 3);
            r.push(tone / 2.0, OctaveSlips::kMaxRunHops + 3);
            QCOMPARE(r.hopsOut(), range(0, 6 + OctaveSlips::kMaxRunHops));
            // Held for as long as it could be a slip, and no longer
            QCOMPARE(r.after[3], 3 + OctaveSlips::kMaxRunHops);
            QCOMPARE(r.after[3 + OctaveSlips::kMaxRunHops],
                     3 + OctaveSlips::kMaxRunHops);
            QCOMPARE(r.after.back(), 5 + OctaveSlips::kMaxRunHops);
            QCOMPARE(r.slips.held(), 0);
        }
    }

    // A run that ends in silence, or at another pitch, may be where the
    // singing went: let through
    void a_run_not_back_on_the_pitch_is_let_through() {
        {
            Run r;
            r.push(tone, 3);
            r.push(tone / 2.0, 2);
            QCOMPARE(r.slips.held(), 2);
            r.push(0.0);
            QCOMPARE(r.hopsOut(), range(0, 5));
            QCOMPARE(r.after.back(), 5);
        }
        {
            Run r;
            r.push(tone, 3);
            r.push(tone / 2.0, 2);
            r.push(tone * 1.5, 2);
            QCOMPARE(r.hopsOut(), range(0, 7));
        }
    }

    // At the start of a phrase there is nothing to be an octave from:
    // the first dot passes, and a run after an unvoiced hop too
    void a_phrase_starts_at_once() {
        Run r;
        r.push(tone / 2.0);
        r.push(0.0);
        r.push(tone, 3);
        QCOMPARE(r.hopsOut(), join(range(0, 1), range(2, 5)));
        QCOMPARE(r.after[0], 0);
        QCOMPARE(r.after[1], 2);
    }

    // Further than the tolerance from an octave is not a slip: a ninth
    // down passes at once
    void other_leaps_pass_at_once() {
        Run r;
        r.push(tone, 3);
        r.push(tone / 2.0 * std::pow(2.0, -150.0 / 1200.0), 2);
        r.push(tone, 3);
        QCOMPARE(r.hopsOut(), range(0, 8));
        for (int i = 0; i < 8; ++i) QCOMPARE(r.after[i], i);
    }
};

#endif
