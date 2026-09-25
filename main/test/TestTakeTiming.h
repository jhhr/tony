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

#ifndef TEST_TAKE_TIMING_H
#define TEST_TAKE_TIMING_H

// Tier 2: the frame arithmetic of a take with a pre-roll and a
// punch-out. No window, no device and no files: what MainWindow does
// with the answers is Tier 5's business (TestRecordWorkflow).

#include "../TakeTiming.h"

#include <QObject>
#include <QtTest>

class TestTakeTiming : public QObject
{
    Q_OBJECT

    typedef sv::sv_frame_t frame_t;

    static constexpr double kRate = 44100.0;

    // A take at P with a pre-roll of R and a round trip of L, running
    // until Stop
    static TakeTiming take(frame_t position, frame_t preRoll = 0,
                           frame_t latency = 0) {
        TakeTiming t;
        t.rate = kRate;
        t.position = position;
        t.preRoll = preRoll;
        t.latency = latency;
        return t;
    }

private slots:
    // The pre-roll is as long as it was asked for, except near the
    // start of the song, where there is less room for it
    void preroll_fits_before_the_position() {
        const frame_t wanted = frame_t(3 * kRate);
        QCOMPARE(TakeTiming::preRollBefore(frame_t(10 * kRate), wanted), wanted);
        QCOMPARE(TakeTiming::preRollBefore(wanted, wanted), wanted);

        // Half a second in, the lead-in is half a second
        QCOMPARE(TakeTiming::preRollBefore(frame_t(0.5 * kRate), wanted),
                 frame_t(0.5 * kRate));
        // and at the start of the song there is none
        QCOMPARE(TakeTiming::preRollBefore(0, wanted), frame_t(0));
        // nor is there with the pre-roll switched off
        QCOMPARE(TakeTiming::preRollBefore(frame_t(10 * kRate), 0), frame_t(0));
    }

    // Playback starts R frames before the take's position, so that the
    // reference reaches the position as the recording starts to count
    void playback_starts_before_the_position() {
        QCOMPARE(take(1000).playbackStart(), frame_t(1000));
        QCOMPARE(take(5000, 3000).playbackStart(), frame_t(2000));
        QCOMPARE(take(3000, 3000).playbackStart(), frame_t(0));
        // Nothing before frame 0, whatever it is asked for
        QCOMPARE(take(1000, 4000).playbackStart(), frame_t(0));
    }

    // The recording is used from the latency plus the lead-in on: both
    // were recorded before the singer could hear the reference at P
    void splice_offset_is_the_latency_and_the_lead_in() {
        QCOMPARE(take(5000).spliceOffset(), frame_t(0));
        QCOMPARE(take(5000, 0, 1500).spliceOffset(), frame_t(1500));
        QCOMPARE(take(5000, 3000).spliceOffset(), frame_t(3000));
        QCOMPARE(take(5000, 3000, 1500).spliceOffset(), frame_t(4500));
        // A latency that was never measured is no latency
        QCOMPARE(take(5000, 3000, -100).spliceOffset(), frame_t(2900));
    }

    // Without a punch-out the whole of the recording is used; with one,
    // only as much of it as reaches the end of the selection
    void splice_length_stops_at_the_end() {
        QCOMPARE(take(5000).spliceLength(), frame_t(-1));

        TakeTiming t = take(5000, 3000, 1500);
        t.end = 9000;
        QVERIFY(t.havePunchOut());
        QCOMPARE(t.spliceLength(), frame_t(4000));
        // ... which the lead-in and the latency do not change
        QCOMPARE(t.spliceOffset(), frame_t(4500));

        // A selection that ends where it starts, or before it, is no
        // punch-out at all
        t.end = 5000;
        QVERIFY(!t.havePunchOut());
        QCOMPARE(t.spliceLength(), frame_t(-1));
    }

    // The take stops itself once the singing for the end of the
    // selection must have arrived: the offset, the length, and a margin
    void auto_stop_waits_for_the_end_and_a_margin() {
        TakeTiming t = take(5000, 3000, 1500);
        QVERIFY(!t.havePunchOut());
        QCOMPARE(t.autoStopFrames(), frame_t(0));
        QVERIFY(!t.shouldStopAt(frame_t(100 * kRate)));

        t.end = 5000 + frame_t(2 * kRate);
        frame_t margin = frame_t(kRate * TakeTiming::autoStopMarginSeconds());
        frame_t want = 4500 + frame_t(2 * kRate) + margin;
        QCOMPARE(t.autoStopFrames(), want);
        QVERIFY(!t.shouldStopAt(want - 1));
        QVERIFY(t.shouldStopAt(want));
        QVERIFY(t.shouldStopAt(want + 10000));

        // A longer lead-in is a later stop: the lead-in is not part of
        // what the take keeps
        TakeTiming longer = t;
        longer.preRoll = 3000 + frame_t(kRate);
        QCOMPARE(longer.autoStopFrames(), want + frame_t(kRate));
    }

    // The live dots go where the finished pitch track will put them,
    // and sound from the lead-in gets no dot at all
    void live_dots_start_at_the_position() {
        TakeTiming t = take(5000, 3000, 1500);
        QCOMPARE(t.liveFrameIntoTake(4500), frame_t(0));
        QCOMPARE(t.liveFrameIntoTake(9500), frame_t(5000));
        QVERIFY(t.liveFrameIntoTake(4499) < 0);
        QVERIFY(t.liveFrameIntoTake(0) < 0);

        // With no lead-in and no latency a dot is where it was sung
        QCOMPARE(take(5000).liveFrameIntoTake(1234), frame_t(1234));
    }

    // The status bar counts the lead-in down in whole seconds, and says
    // nothing once the take's own material has begun
    void countdown_runs_out_with_the_lead_in() {
        TakeTiming t = take(frame_t(10 * kRate), frame_t(3 * kRate));
        QVERIFY(t.isInLeadIn(0));
        QCOMPARE(t.countdownSeconds(0), 3);
        QCOMPARE(t.countdownText(0), QString("Recording in 3…"));
        QCOMPARE(t.countdownSeconds(frame_t(1.5 * kRate)), 2);
        QCOMPARE(t.countdownText(frame_t(1.5 * kRate)),
                 QString("Recording in 2…"));
        QCOMPARE(t.countdownSeconds(frame_t(3 * kRate) - 1), 1);

        // Once the lead-in is over there is nothing to say
        QVERIFY(!t.isInLeadIn(frame_t(3 * kRate)));
        QCOMPARE(t.countdownSeconds(frame_t(3 * kRate)), 0);
        QCOMPARE(t.countdownText(frame_t(3 * kRate)), QString());
        QCOMPARE(t.countdownText(frame_t(10 * kRate)), QString());

        // The latency is part of the wait: the singer's answer to the
        // reference at P arrives that much later
        t.latency = frame_t(0.5 * kRate);
        QCOMPARE(t.countdownSeconds(frame_t(3 * kRate)), 1);

        // With the pre-roll off there is no lead-in and no countdown,
        // however long the round trip is
        TakeTiming plain = take(frame_t(10 * kRate), 0, frame_t(0.5 * kRate));
        QVERIFY(!plain.isInLeadIn(0));
        QCOMPARE(plain.countdownText(0), QString());
    }

    // A device at 48 kHz, as phones are, against a reference at 44.1:
    // P, E and R are the reference's frames, L and every count off the
    // record target the recording's, and each answer is in the frames it
    // is used in
    void device_at_another_rate() {
        const double deviceRate = 48000.0;
        TakeTiming t = take(frame_t(10 * kRate), frame_t(0.5 * kRate),
                            frame_t(0.1 * deviceRate));
        t.recordRate = deviceRate;

        QCOMPARE(t.recordedToReference(frame_t(deviceRate)), frame_t(kRate));
        QCOMPARE(t.referenceToRecorded(frame_t(kRate)), frame_t(deviceRate));
        QCOMPARE(t.recordedToReference(12345), frame_t(11342));

        // The splice reads the recording once it is at the reference's
        // rate: the latency is converted, the lead-in is not
        QCOMPARE(t.spliceOffset(), frame_t(0.1 * kRate) + frame_t(0.5 * kRate));
        QCOMPARE(t.playbackStart(), frame_t(9.5 * kRate));

        // The record target counts the device's frames: the take waits
        // for the latency, the lead-in, two seconds of selection and the
        // margin, all at the device's rate
        t.end = t.position + frame_t(2 * kRate);
        QCOMPARE(t.spliceLength(), frame_t(2 * kRate));
        frame_t want = frame_t(0.1 * deviceRate) + frame_t(0.5 * deviceRate) +
            frame_t(2 * deviceRate) +
            frame_t(TakeTiming::autoStopMarginSeconds() * deviceRate);
        QCOMPARE(t.autoStopFrames(), want);
        QVERIFY(!t.shouldStopAt(want - 1));
        QVERIFY(t.shouldStopAt(want));

        // A dot for what the device recorded one second after the lead-in
        // is one second into the take, at the reference's rate
        frame_t lead = frame_t(0.1 * deviceRate) + frame_t(0.5 * deviceRate);
        QCOMPARE(t.liveFrameIntoTake(lead), frame_t(0));
        QCOMPARE(t.liveFrameIntoTake(lead + frame_t(deviceRate)), frame_t(kRate));
        QVERIFY(t.liveFrameIntoTake(lead - 100) < 0);

        // The countdown goes by the device's frames too
        TakeTiming c = take(frame_t(10 * kRate), frame_t(3 * kRate));
        c.recordRate = deviceRate;
        QCOMPARE(c.countdownSeconds(0), 3);
        QCOMPARE(c.countdownSeconds(frame_t(1.5 * deviceRate)), 2);
        QCOMPARE(c.countdownSeconds(frame_t(3 * deviceRate) - 100), 1);
        QVERIFY(c.isInLeadIn(frame_t(3 * deviceRate) - 100));
        QVERIFY(!c.isInLeadIn(frame_t(3 * deviceRate)));
        QCOMPARE(c.countdownSeconds(frame_t(3 * deviceRate)), 0);

        // At the reference's rate, or with none given, nothing changes
        TakeTiming same = take(5000, 3000, 1500);
        same.recordRate = kRate;
        QCOMPARE(same.recordedToReference(12345), frame_t(12345));
        QCOMPARE(same.spliceOffset(), frame_t(4500));
        QCOMPARE(take(5000, 3000, 1500).recordedToReference(12345),
                 frame_t(12345));
    }

    // Recording into a selection: the one the playhead is in, else the
    // first of them
    void selection_at_the_playhead_is_the_one_recorded_into() {
        Coverage::Ranges ranges;
        Coverage::Range chosen;

        QVERIFY(!TakeTiming::chooseRange(ranges, 100, chosen));

        ranges.push_back(Coverage::Range(1000, 2000));
        ranges.push_back(Coverage::Range(5000, 7000));

        QVERIFY(TakeTiming::chooseRange(ranges, 6000, chosen));
        QCOMPARE(chosen, Coverage::Range(5000, 7000));
        QVERIFY(TakeTiming::chooseRange(ranges, 5000, chosen));
        QCOMPARE(chosen, Coverage::Range(5000, 7000));

        // The end of a range is not in it
        QVERIFY(TakeTiming::chooseRange(ranges, 2000, chosen));
        QCOMPARE(chosen, Coverage::Range(1000, 2000));

        // The playhead outside all of them: the first
        QVERIFY(TakeTiming::chooseRange(ranges, 0, chosen));
        QCOMPARE(chosen, Coverage::Range(1000, 2000));
        QVERIFY(TakeTiming::chooseRange(ranges, 100000, chosen));
        QCOMPARE(chosen, Coverage::Range(1000, 2000));
    }
};

#endif
