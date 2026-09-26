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

#ifndef TEST_STREAM_LATENCY_H
#define TEST_STREAM_LATENCY_H

// Tier 2: a device's latency from its streams' timestamps, as the
// Android backend works it out. Counters and timestamps in, frames out;
// no device: a simulated one whose true latencies are known.

#include "../StreamLatency.h"

#include <QObject>
#include <QtTest>

#include <cmath>
#include <vector>

class TestStreamLatency : public QObject
{
    Q_OBJECT

    // A duplex device at 48 kHz with callbacks of 96 frames, every 2 ms
    // from t0. Each callback reads 96 frames of input and writes 96 of
    // output. The frame written in callback k is heard trueOutput frames
    // after the callback; the frame read in callback k came in trueInput
    // frames before it. The hardware's timestamps are exact.
    struct Device {
        static constexpr double rate = 48000.0;
        static constexpr int burst = 96;
        static constexpr int64_t t0 = 5000000000; // ns
        double trueOutput = 250.0;
        double trueInput = 130.0;

        static int64_t nanos(double frames) {
            return int64_t(std::llround(frames * 1.0e9 / rate));
        }
        // When output frame f is heard, and input frame f came in
        int64_t heard(int64_t f) const {
            return t0 + nanos(double(f) + trueOutput);
        }
        int64_t cameIn(int64_t f) const {
            return t0 + nanos(double(f) - trueInput);
        }
        // What the streams report at a moment after callback k has run
        // and before k + 1 does: every frame of callback k is written
        // and read, and the timestamps are of frames some time back
        StreamLatency::Position output(int k) const {
            StreamLatency::Position p;
            p.appFrames = int64_t(k + 1) * burst;
            p.hardwareFrame = p.appFrames - 400;
            p.hardwareNanos = heard(p.hardwareFrame);
            return p;
        }
        StreamLatency::Position input(int k) const {
            StreamLatency::Position p;
            p.appFrames = int64_t(k + 1) * burst;
            p.hardwareFrame = p.appFrames - 700;
            p.hardwareNanos = cameIn(p.hardwareFrame);
            return p;
        }
        // The moment a fraction of the way from callback k to k + 1
        int64_t between(int k, double fraction) const {
            return t0 + nanos((double(k) + fraction) * burst);
        }
    };

    static StreamLatency::Estimate est(int output, int input) {
        StreamLatency::Estimate e;
        e.output = output;
        e.input = input;
        return e;
    }

private slots:
    void output_latency_is_what_is_written_and_not_yet_heard() {
        // 1000 frames written beyond the timestamp's frame, which went
        // out 10 ms (480 frames) ago
        StreamLatency::Position p;
        p.appFrames = 10000;
        p.hardwareFrame = 9000;
        p.hardwareNanos = 1000000000;
        QCOMPARE(StreamLatency::outputLatency(p, 1010000000, 48000.0), 520.0);
    }

    void input_latency_is_the_age_of_the_next_frame_read() {
        // The next frame to read came in 1000 frames after the
        // timestamp's, and 25 ms (1200 frames) have gone by since it
        StreamLatency::Position p;
        p.appFrames = 10000;
        p.hardwareFrame = 9000;
        p.hardwareNanos = 1000000000;
        QCOMPARE(StreamLatency::inputLatency(p, 1025000000, 48000.0), 200.0);
        // Read so eagerly that the next frame is not in yet
        QCOMPARE(StreamLatency::inputLatency(p, 1015000000, 48000.0), -280.0);
    }

    // Read at any moment between two callbacks, each part is off by the
    // time to the next callback, and their sum is the true round trip
    void round_trip_does_not_depend_on_when_it_is_read() {
        Device d;
        for (int k : { 10, 11, 500 }) {
            for (double fraction : { 0.05, 0.3, 0.6, 0.95 }) {
                int64_t now = d.between(k, fraction);
                double out = StreamLatency::outputLatency
                    (d.output(k), now, Device::rate);
                double in = StreamLatency::inputLatency
                    (d.input(k), now, Device::rate);
                QVERIFY(std::fabs(out + in - (d.trueOutput + d.trueInput))
                        < 0.01);
                // the time to the next callback, one way and the other
                double toNext = (1.0 - fraction) * Device::burst;
                QVERIFY(std::fabs(out - (d.trueOutput + toNext)) < 0.01);
                QVERIFY(std::fabs(in - (d.trueInput - toNext)) < 0.01);
            }
        }
    }

    // What the figure is for: the singer answers output frame W the
    // moment it is heard, and the answer comes in at input frame R plus
    // the round trip, R and W being where the two streams stand at the
    // same callback (recording.md, "Latency")
    void round_trip_is_where_an_answer_to_the_output_lands() {
        Device d;
        d.trueOutput = 1234.0;
        d.trueInput = 321.0;
        int k = 40;
        int64_t now = d.between(k, 0.7);
        StreamLatency::Estimate e = StreamLatency::fromReading
            (StreamLatency::outputLatency(d.output(k), now, Device::rate),
             StreamLatency::inputLatency(d.input(k), now, Device::rate));

        int64_t w = d.output(k).appFrames; // the next frame to be written
        int64_t r = d.input(k).appFrames;  // and to be read
        // The input frame that came in when output frame w was heard
        int64_t answer = r;
        while (d.cameIn(answer) < d.heard(w)) ++answer;
        QCOMPARE(r + e.roundTrip(), answer);
    }

    void reading_is_rounded_once() {
        StreamLatency::Estimate e = StreamLatency::fromReading(100.4, 50.4);
        QCOMPARE(e.roundTrip(), 151);
        QCOMPARE(e.output, 100);
        QCOMPARE(e.input, 51);
    }

    // Bqaudioio's users take a negative latency as none (LatencyUtils.h
    // does), which would lose it from the sum
    void negative_part_is_taken_off_the_other() {
        StreamLatency::Estimate e = StreamLatency::fromReading(300.4, -50.2);
        QCOMPARE(e.output, 250);
        QCOMPARE(e.input, 0);

        e = StreamLatency::fromReading(-20.0, 500.0);
        QCOMPARE(e.output, 0);
        QCOMPARE(e.input, 480);

        e = StreamLatency::fromReading(-20.0, -30.0);
        QCOMPARE(e.output, 0);
        QCOMPARE(e.input, 0);
    }

    void plausible_is_more_than_nothing_and_at_most_a_second() {
        QVERIFY(StreamLatency::isPlausible(est(200, 100), 48000.0));
        QVERIFY(StreamLatency::isPlausible(est(48000, 0), 48000.0));
        QVERIFY(!StreamLatency::isPlausible(est(48000, 1), 48000.0));
        QVERIFY(!StreamLatency::isPlausible(est(0, 0), 48000.0));
    }

    void median_takes_the_middle_round_trip() {
        StreamLatency::Estimate chosen;
        // One reading spoilt by a callback, one burst low; one absurd
        std::vector<StreamLatency::Estimate> readings {
            est(260, 120), est(250, 130), est(160, 124),
            est(900000, 0), est(270, 110), est(255, 127),
        };
        QVERIFY(StreamLatency::median(readings, 48000.0, chosen));
        QCOMPARE(chosen.roundTrip(), 380);

        readings = { est(100, 0) };
        QVERIFY(StreamLatency::median(readings, 48000.0, chosen));
        QCOMPARE(chosen.output, 100);

        readings = { est(0, 0), est(-5, 0) };
        QVERIFY(!StreamLatency::median(readings, 48000.0, chosen));
        QVERIFY(!StreamLatency::median({}, 48000.0, chosen));
    }

    void guess_is_the_output_buffer_and_a_burst_of_input() {
        StreamLatency::Estimate e = StreamLatency::guess(192, 96);
        QCOMPARE(e.output, 192);
        QCOMPARE(e.input, 96);
        e = StreamLatency::guess(-1, -1);
        QCOMPARE(e.roundTrip(), 0);
    }

    // A callback reads all the input there is, up to its room, and at
    // least what the output asks for; after a stall of 244 ms, as on the
    // phone, all of it at once, where FullDuplexStream would read 96
    // frames a callback and never catch up
    void a_callback_reads_all_the_input_there_is() {
        QCOMPARE(StreamLatency::inputFramesToRead(96, 11424, 11520), 11424);
        QCOMPARE(StreamLatency::inputFramesToRead(96, 20000, 11520), 11520);
        QCOMPARE(StreamLatency::inputFramesToRead(96, 192, 11520), 192);
        QCOMPARE(StreamLatency::inputFramesToRead(96, 0, 11520), 96);
        QCOMPARE(StreamLatency::inputFramesToRead(96, 40, 11520), 96);
        QCOMPARE(StreamLatency::inputFramesToRead(96, 500, 0), 0);
    }

    // What a callback that keeps up leaves waiting is the output's buffer
    // and two input bursts at most, on the phone's streams 192 and 96: a
    // reading with the input's whole buffer waiting is refused
    void a_reading_is_used_only_while_the_input_keeps_up() {
        QVERIFY(StreamLatency::inputKeptUp(0, 192, 96));
        QVERIFY(StreamLatency::inputKeptUp(170, 192, 96));
        QVERIFY(StreamLatency::inputKeptUp(384, 192, 96));
        QVERIFY(!StreamLatency::inputKeptUp(385, 192, 96));
        QVERIFY(!StreamLatency::inputKeptUp(11424, 192, 96));
        QVERIFY(StreamLatency::inputKeptUp(0, -1, -1));
        QVERIFY(!StreamLatency::inputKeptUp(1, 0, 0));
    }
};

#endif
