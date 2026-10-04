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

#ifndef TEST_RECORDING_ALIGNMENT_H
#define TEST_RECORDING_ALIGNMENT_H

// Tier 2: where a take's audio sits in a transmitter's own recording of
// the same singing. No window, no files: synthetic singing in memory, a
// phrase of notes with harmonics, vibrato and breath, rendered from
// functions of time so that it is the same singing at any rate and on
// any clock.

#include "../RecordingAlignment.h"

#include <QObject>
#include <QtTest>

#include <cmath>
#include <random>
#include <vector>

class TestRecordingAlignment : public QObject
{
    Q_OBJECT

    static constexpr double pi = 3.14159265358979323846;
    static constexpr double takeRate = 44100.0;

    // A sung phrase as a function of time from its start, in seconds
    struct Phrase {
        struct Note {
            double onset, length, hz, amp, phase, vibratoPhase;
            double harmonicPhase[6];
        };
        struct Hiss { double hz, phase, amp; };
        std::vector<Note> notes;
        std::vector<Hiss> breath;
        double duration = 0;

        double value(double t) const {
            if (t < 0 || t >= duration) return 0.0;
            double v = 0.0;
            for (const Note &n : notes) {
                const double tau = t - n.onset;
                if (tau < 0 || tau >= n.length) continue;
                const double env = std::min({ 1.0, tau / 0.03,
                                              (n.length - tau) / 0.05 });
                // About 30 cents of vibrato at 5.5 Hz, its phase integrated
                const double depth = 0.017, rate = 5.5;
                const double phase = 2 * pi * n.hz *
                    (tau - depth / (2 * pi * rate) *
                     (std::cos(2 * pi * rate * tau + n.vibratoPhase) -
                      std::cos(n.vibratoPhase))) + n.phase;
                double s = 0.0;
                for (int h = 1; h <= 6; ++h) {
                    s += std::sin(h * phase + n.harmonicPhase[h - 1]) / h;
                }
                v += n.amp * env * s * 0.5;
            }
            for (const Hiss &b : breath) {
                v += b.amp * std::sin(2 * pi * b.hz * t + b.phase);
            }
            return v;
        }
    };

    // The notes of a song from songSeed; sung, from singingSeed, with
    // the timing moved by up to jitter seconds each way and phases,
    // vibrato and breath of its own
    static Phrase phrase(unsigned songSeed, double seconds,
                         unsigned singingSeed = 0, double jitter = 0.0) {
        std::mt19937 song(songSeed);
        std::mt19937 singing(singingSeed * 7919u + songSeed);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        const int scale[] = { 0, 2, 4, 5, 7, 9, 11, 12 };
        Phrase p;
        p.duration = seconds;
        double t = 0.05;
        while (t < seconds - 0.3) {
            Phrase::Note n;
            const double length = 0.25 + 0.45 * u(song);
            const double rest = 0.15 * u(song);
            n.hz = 196.0 * std::pow(2.0, scale[int(u(song) * 8) % 8] / 12.0);
            n.amp = 0.3 + 0.5 * u(song);
            n.onset = t + jitter * (2 * u(singing) - 1);
            n.length = std::min(length + jitter * (2 * u(singing) - 1),
                                seconds - n.onset);
            n.phase = 2 * pi * u(singing);
            n.vibratoPhase = 2 * pi * u(singing);
            for (double &h : n.harmonicPhase) h = 2 * pi * u(singing);
            if (n.length > 0.1) p.notes.push_back(n);
            t += length + rest;
        }
        for (int k = 0; k < 16; ++k) {
            p.breath.push_back({ 300.0 + 6000.0 * u(singing),
                                 2 * pi * u(singing), 0.002 });
        }
        return p;
    }

    // A take's file: silence but for the phrase from frame "from"
    static std::vector<float> takeOf(const Phrase &p, double seconds,
                                     sv::sv_frame_t from) {
        std::vector<float> v(size_t(seconds * takeRate), 0.f);
        for (size_t i = 0; i < v.size(); ++i) {
            v[i] = float(p.value((double(i) - double(from)) / takeRate));
        }
        return v;
    }

    // A recording of the given phrases, each from its start time, at a
    // rate and gain, on a clock that runs fast by ppm, over a floor of
    // noise at about -66 dBFS
    struct Placed { const Phrase *phrase; double start; };
    static std::vector<float> recordingOf(const std::vector<Placed> &placed,
                                          double seconds, double rate,
                                          double gain = 1.0,
                                          double ppm = 0.0) {
        std::mt19937 noise(99);
        std::normal_distribution<double> g(0.0, 0.0005);
        std::vector<float> v(size_t(seconds * rate), 0.f);
        const double clock = rate * (1.0 + ppm * 1e-6);
        for (size_t i = 0; i < v.size(); ++i) {
            const double t = double(i) / clock;
            double s = 0.0;
            for (const Placed &p : placed) s += p.phrase->value(t - p.start);
            v[i] = float(gain * s + g(noise));
        }
        return v;
    }

    // Radio dropouts: a short one every 80 to 150 ms, of 10 to 30 ms, and
    // now and then a long one, of 0.3 to 0.8 s: digital silence
    static int addDropouts(std::vector<float> &take, sv::sv_frame_t from,
                           sv::sv_frame_t to) {
        std::mt19937 r(5);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        sv::sv_frame_t at = from + sv::sv_frame_t(0.2 * takeRate);
        sv::sv_frame_t dropped = 0;
        int count = 0;
        while (at < to) {
            const bool longOne = (u(r) < 0.06);
            const double seconds = longOne ? 0.3 + 0.5 * u(r) :
                0.01 + 0.02 * u(r);
            const sv::sv_frame_t end =
                std::min(to, at + sv::sv_frame_t(seconds * takeRate));
            for (sv::sv_frame_t i = at; i < end; ++i) take[size_t(i)] = 0.f;
            dropped += end - at;
            ++count;
            at = end + sv::sv_frame_t((0.08 + 0.07 * u(r)) * takeRate);
        }
        return int(100 * dropped / (to - from));
    }

    QString describe(const RecordingAlignment::Match &m) {
        return QString("found %1, offset %2, confidence %3 over %4 pieces, "
                       "ends %5 and %6, gain %7: %8")
            .arg(m.found).arg(m.offset, 0, 'f', 2)
            .arg(m.confidence, 0, 'f', 3).arg(m.pieces)
            .arg(m.startOffset, 0, 'f', 1).arg(m.endOffset, 0, 'f', 1)
            .arg(m.gain, 0, 'f', 3).arg(m.error);
    }

private slots:
    // The correlation coefficient at each offset, the masked samples left
    // out: a scaled and shifted copy is alike whatever the mask hides
    void the_masked_correlation() {
        std::mt19937 r(1);
        std::uniform_real_distribution<double> u(-1.0, 1.0);
        std::vector<double> a(500), b(2000), mask(500, 1.0);
        for (double &x : a) x = u(r);
        for (double &x : b) x = u(r);
        for (size_t j = 0; j < a.size(); ++j) b[j + 737] = 3.0 * a[j] + 0.5;
        auto c = RecordingAlignment::maskedCorrelation(a, mask, b);
        QCOMPARE(int(c.size()), 1501);
        QVERIFY(std::fabs(c[737] - 1.0) < 1e-9);
        for (size_t L = 0; L < c.size(); ++L) {
            if (L != 737) QVERIFY2(c[L] < 0.3, qPrintable(QString::number(L)));
        }
        // Spoil a third of the copy, and mask it
        for (size_t j = 100; j < 270; ++j) {
            b[j + 737] = 5.0;
            mask[j] = 0.0;
        }
        c = RecordingAlignment::maskedCorrelation(a, mask, b);
        QVERIFY(std::fabs(c[737] - 1.0) < 1e-9);
        // A silent stretch is alike to nothing
        std::vector<double> silent(2000, 0.0);
        c = RecordingAlignment::maskedCorrelation(a, mask, silent);
        for (double x : c) QCOMPARE(x, 0.0);

        // A few offsets, summed directly, as the whole range does
        const std::vector<double> few(b.begin() + 720, b.begin() + 1250);
        const auto d = RecordingAlignment::maskedCorrelation(a, mask, few);
        QCOMPARE(int(d.size()), 31);
        c = RecordingAlignment::maskedCorrelation(a, mask, b);
        for (size_t L = 0; L < d.size(); ++L) {
            QVERIFY(std::fabs(d[L] - c[L + 720]) < 1e-9);
        }
        QVERIFY(std::fabs(d[17] - 1.0) < 1e-9);
    }

    // Dropouts are runs of digital silence of 5 ms or more; a zero
    // crossing, or a shorter run, is singing
    void dropouts_are_runs_of_silence() {
        std::vector<float> v(10000, 0.1f);
        for (int i = 1000; i < 1100; ++i) v[size_t(i)] = 0.f;  // 2.3 ms
        for (int i = 3000; i < 3300; ++i) v[size_t(i)] = 0.f;  // 6.8 ms
        v[5000] = 0.f;
        const auto gaps = RecordingAlignment::dropouts(v, takeRate);
        int count = 0;
        for (size_t i = 0; i < gaps.size(); ++i) {
            if (gaps[i]) {
                ++count;
                QVERIFY(i >= 3000 && i < 3300);
            }
        }
        QCOMPARE(count, 300);
    }

    // At the take's rate the offset is found to the frame, the two ends
    // agree, and the gain is the take's to the recording's
    void the_offset_is_found_to_the_frame() {
        const Phrase sung = phrase(1, 8.0);
        const Phrase before = phrase(2, 9.0), after = phrase(3, 10.0);
        const sv::sv_frame_t from = 44100 + 77;
        const sv::sv_frame_t to = from + sv::sv_frame_t(8.0 * takeRate);
        RecordingAlignment::MemorySource take(takeOf(sung, 12.0, from), takeRate);
        // The phrase 12.0279... s into the recording, on a frame of it
        const double start = 12.0 + 1234.0 / takeRate;
        RecordingAlignment::MemorySource recording
            (recordingOf({ { &before, 1.0 }, { &sung, start },
                           { &after, 23.0 } }, 40.0, takeRate),
             takeRate);

        const auto m = RecordingAlignment::find(take, from, to, recording);
        QVERIFY2(m.found, qPrintable(describe(m)));
        QCOMPARE(m.offset, double(12 * 44100 + 1234 - from));
        QVERIFY2(m.confidence > 0.95, qPrintable(describe(m)));
        QVERIFY2(m.pieces >= 20, qPrintable(describe(m)));
        QVERIFY(m.endsMeasured);
        QCOMPARE(m.drift(takeRate), 0.0);
        QVERIFY2(std::fabs(m.gain - 1.0) < 0.02, qPrintable(describe(m)));
    }

    // A recording 22 dB quieter, over its own noise, is found as well
    void a_gain_difference_does_not_matter() {
        const Phrase sung = phrase(4, 6.0);
        const sv::sv_frame_t from = 22050;
        const sv::sv_frame_t to = from + sv::sv_frame_t(6.0 * takeRate);
        RecordingAlignment::MemorySource take(takeOf(sung, 8.0, from), takeRate);
        const double start = 3.0 + 500.0 / takeRate;
        RecordingAlignment::MemorySource recording
            (recordingOf({ { &sung, start } }, 20.0, takeRate, 0.08),
             takeRate);

        const auto m = RecordingAlignment::find(take, from, to, recording);
        QVERIFY2(m.found, qPrintable(describe(m)));
        QCOMPARE(m.offset, double(3 * 44100 + 500 - from));
        QVERIFY2(m.confidence > 0.9, qPrintable(describe(m)));
        QVERIFY2(std::fabs(m.gain - 12.5) < 0.6, qPrintable(describe(m)));
    }

    // A take full of dropout gaps, a fifth or more of it silent, is
    // found to the frame, and reads as alike as a whole one: the gaps
    // are left out of the comparison, not counted as unlike
    void dropout_gaps_in_the_take() {
        const Phrase sung = phrase(5, 10.0);
        const sv::sv_frame_t from = 44100;
        const sv::sv_frame_t to = from + sv::sv_frame_t(10.0 * takeRate);
        std::vector<float> audio = takeOf(sung, 12.0, from);
        const int percent = addDropouts(audio, from, to);
        QVERIFY2(percent >= 20, qPrintable(QString::number(percent)));
        RecordingAlignment::MemorySource take(audio, takeRate);
        const double start = 30.0 + 4321.0 / takeRate;
        const Phrase other = phrase(6, 10.0);
        RecordingAlignment::MemorySource recording
            (recordingOf({ { &other, 5.0 }, { &sung, start } }, 50.0,
                         takeRate, 0.5),
             takeRate);

        const auto m = RecordingAlignment::find(take, from, to, recording);
        QVERIFY2(m.found, qPrintable(describe(m)));
        QCOMPARE(m.offset, double(30 * 44100 + 4321 - from));
        QVERIFY2(m.confidence > 0.95, qPrintable(describe(m)));
        QVERIFY2(std::fabs(m.gain - 2.0) < 0.1, qPrintable(describe(m)));
    }

    // A recording at 48 kHz, as a transmitter's is: the offset in the
    // take's frames, within a frame of where the singing is
    void a_recording_at_another_rate() {
        const Phrase sung = phrase(7, 8.0);
        const sv::sv_frame_t from = 3 * 44100;
        const sv::sv_frame_t to = from + sv::sv_frame_t(8.0 * takeRate);
        RecordingAlignment::MemorySource take(takeOf(sung, 12.0, from), takeRate);
        const double start = 5.4321;
        RecordingAlignment::MemorySource recording
            (recordingOf({ { &sung, start } }, 20.0, 48000.0, 0.7), 48000.0);

        const auto m = RecordingAlignment::find(take, from, to, recording);
        QVERIFY2(m.found, qPrintable(describe(m)));
        const double expected = start * takeRate - double(from);
        QVERIFY2(std::fabs(m.offset - expected) <= 1.0,
                 qPrintable(QString("expected %1; ").arg(expected) +
                            describe(m)));
        QVERIFY2(m.confidence > 0.9, qPrintable(describe(m)));
        QVERIFY2(std::fabs(m.drift(takeRate)) < 0.0001, qPrintable(describe(m)));
    }

    // Nothing of the take in the recording: not found, and why
    void no_match() {
        const Phrase sung = phrase(8, 6.0);
        const Phrase a = phrase(9, 8.0), b = phrase(10, 8.0);
        const sv::sv_frame_t from = 44100;
        const sv::sv_frame_t to = from + sv::sv_frame_t(6.0 * takeRate);
        RecordingAlignment::MemorySource take(takeOf(sung, 8.0, from), takeRate);
        RecordingAlignment::MemorySource recording
            (recordingOf({ { &a, 1.0 }, { &b, 11.0 } }, 20.0, takeRate),
             takeRate);

        const auto m = RecordingAlignment::find(take, from, to, recording);
        QVERIFY2(!m.found, qPrintable(describe(m)));
        QVERIFY2(m.confidence < 0.3, qPrintable(describe(m)));
        QVERIFY(m.error.contains("not in the recording"));
    }

    // The recording holds another singing of the same song, the same
    // notes 30 ms or so earlier or later, before the take's own: the
    // take's own is found. With only the other, nothing is
    void another_singing_of_the_song_is_not_the_take() {
        const Phrase sung = phrase(11, 8.0, 1);
        const Phrase again = phrase(11, 8.0, 2, 0.03);
        const sv::sv_frame_t from = 44100;
        const sv::sv_frame_t to = from + sv::sv_frame_t(8.0 * takeRate);
        RecordingAlignment::MemorySource take(takeOf(sung, 10.0, from), takeRate);
        RecordingAlignment::MemorySource both
            (recordingOf({ { &again, 2.0 }, { &sung, 14.0 } }, 25.0,
                         takeRate),
             takeRate);
        auto m = RecordingAlignment::find(take, from, to, both);
        QVERIFY2(m.found, qPrintable(describe(m)));
        QCOMPARE(m.offset, double(14 * 44100 - from));

        RecordingAlignment::MemorySource other
            (recordingOf({ { &again, 2.0 } }, 12.0, takeRate), takeRate);
        m = RecordingAlignment::find(take, from, to, other);
        QVERIFY2(!m.found, qPrintable(describe(m)));
        QVERIFY2(m.confidence < 0.3, qPrintable(describe(m)));

        // Nor any part of it, as a range not found is looked into
        std::vector<double> levels;
        QVERIFY(RecordingAlignment::levels(other, levels));
        for (const auto &s : RecordingAlignment::findSegments
                 (take, from, to, other, levels)) {
            QVERIFY2(!s.match.found,
                     qPrintable(QString("[%1, %2): ").arg(s.start).arg(s.end) +
                                describe(s.match)));
        }
    }

    // A transmitter whose clock runs 100 ppm fast: 3 ms over 30 s,
    // followed through the take and measured at its two ends
    void clock_drift_is_measured() {
        const Phrase sung = phrase(12, 30.0);
        const sv::sv_frame_t from = 44100;
        const sv::sv_frame_t to = from + sv::sv_frame_t(30.0 * takeRate);
        RecordingAlignment::MemorySource take(takeOf(sung, 32.0, from), takeRate);
        RecordingAlignment::MemorySource recording
            (recordingOf({ { &sung, 2.0 } }, 35.0, takeRate, 1.0, 100.0),
             takeRate);

        const auto m = RecordingAlignment::find(take, from, to, recording);
        QVERIFY2(m.found, qPrintable(describe(m)));
        QVERIFY(m.endsMeasured);
        // The ends' pieces are a little inside the range's ends
        const double drift = m.drift(takeRate);
        QVERIFY2(drift > 0.0026 && drift < 0.0031,
                 qPrintable(QString("drift %1 ms; ").arg(drift * 1000) +
                            describe(m)));
        QVERIFY2(m.confidence > 0.9, qPrintable(describe(m)));
        QVERIFY(std::fabs(drift) > RecordingAlignment::kDriftSeconds);
    }

    // The same with a dropout of 12 s in the middle, over which the
    // clocks drift 1.2 ms: the walk takes up the singing again after it
    void clock_drift_over_a_long_dropout() {
        const Phrase sung = phrase(12, 30.0);
        const sv::sv_frame_t from = 44100;
        const sv::sv_frame_t to = from + sv::sv_frame_t(30.0 * takeRate);
        std::vector<float> audio = takeOf(sung, 32.0, from);
        for (sv::sv_frame_t i = from + sv::sv_frame_t(9.0 * takeRate);
             i < from + sv::sv_frame_t(21.0 * takeRate); ++i) {
            audio[size_t(i)] = 0.f;
        }
        RecordingAlignment::MemorySource take(audio, takeRate);
        RecordingAlignment::MemorySource recording
            (recordingOf({ { &sung, 2.0 } }, 35.0, takeRate, 1.0, 100.0),
             takeRate);

        const auto m = RecordingAlignment::find(take, from, to, recording);
        QVERIFY2(m.found, qPrintable(describe(m)));
        QVERIFY2(m.confidence > 0.9, qPrintable(describe(m)));
        const double drift = m.drift(takeRate);
        QVERIFY2(drift > 0.0026 && drift < 0.0031,
                 qPrintable(QString("drift %1 ms; ").arg(drift * 1000) +
                            describe(m)));
    }

    // A take of 20 s with a punch-in from 8.13 s to 13.07 s, off the
    // walk's quarter seconds: another singing of the same song, which the
    // transmitter recorded later. Three segments, each found where it
    // was sung, the switches within a rest of where they were
    void a_punch_in_is_found_where_it_was_sung() {
        const Phrase first = phrase(20, 20.0, 1);
        const Phrase second = phrase(20, 20.0, 2, 0.03);
        const sv::sv_frame_t from = 44100;
        const sv::sv_frame_t to = from + sv::sv_frame_t(20.0 * takeRate);
        const sv::sv_frame_t in = from + sv::sv_frame_t(8.13 * takeRate);
        const sv::sv_frame_t out = from + sv::sv_frame_t(13.07 * takeRate);
        std::vector<float> audio = takeOf(first, 22.0, from);
        const std::vector<float> punch = takeOf(second, 22.0, from);
        std::copy(punch.begin() + in, punch.begin() + out, audio.begin() + in);
        RecordingAlignment::MemorySource take(audio, takeRate);
        RecordingAlignment::MemorySource recording
            (recordingOf({ { &first, 3.0 }, { &second, 30.0 } }, 52.0,
                         takeRate),
             takeRate);

        std::vector<double> levels;
        QVERIFY(RecordingAlignment::levels(recording, levels));
        const auto segments = RecordingAlignment::findSegments
            (take, from, to, recording, levels);
        QString all;
        for (const auto &s : segments) {
            all += QString("[%1, %2) at %3, %4; ").arg(s.start).arg(s.end)
                .arg(s.match.offset).arg(s.match.found);
        }
        QCOMPARE(int(segments.size()), 3);
        QCOMPARE(segments[0].start, from);
        QCOMPARE(segments[2].end, to);
        QCOMPARE(segments[0].match.offset, double(3 * 44100 - from));
        QCOMPARE(segments[1].match.offset, double(30 * 44100 - from));
        QCOMPARE(segments[2].match.offset, double(3 * 44100 - from));
        QVERIFY2(std::llabs(segments[1].start - in) < 0.08 * takeRate &&
                 std::llabs(segments[1].end - out) < 0.08 * takeRate,
                 qPrintable(all));
        QCOMPARE(segments[0].end, segments[1].start);
        QCOMPARE(segments[1].end, segments[2].start);
        for (const auto &s : segments) QVERIFY2(s.match.found, qPrintable(all));
    }

    // Most of the take is the punch-in, so that the take as a whole is
    // not alike either singing: found by the run of either that is
    void a_take_mostly_punched_in_is_found_by_its_sessions() {
        const Phrase first = phrase(21, 20.0, 1);
        const Phrase second = phrase(21, 20.0, 2, 0.03);
        const sv::sv_frame_t from = 0;
        const sv::sv_frame_t to = sv::sv_frame_t(20.0 * takeRate);
        const sv::sv_frame_t in = sv::sv_frame_t(6.0 * takeRate);
        std::vector<float> audio = takeOf(first, 21.0, from);
        const std::vector<float> punch = takeOf(second, 21.0, from);
        std::copy(punch.begin() + in, punch.begin() + to, audio.begin() + in);
        RecordingAlignment::MemorySource take(audio, takeRate);
        RecordingAlignment::MemorySource recording
            (recordingOf({ { &first, 2.0 }, { &second, 25.0 } }, 46.0,
                         takeRate),
             takeRate);

        std::vector<double> levels;
        QVERIFY(RecordingAlignment::levels(recording, levels));
        const auto segments = RecordingAlignment::findSegments
            (take, from, to, recording, levels);
        QString all;
        for (const auto &s : segments) {
            all += QString("[%1, %2) at %3, %4; ").arg(s.start).arg(s.end)
                .arg(s.match.offset).arg(s.match.found);
        }
        QVERIFY2(segments.size() == 2, qPrintable(all));
        QCOMPARE(segments[0].match.offset, double(2 * 44100));
        QCOMPARE(segments[1].match.offset, double(25 * 44100));
        QVERIFY2(std::llabs(segments[0].end - in) < 0.1 * takeRate,
                 qPrintable(all));
        QCOMPARE(segments[1].end, to);
    }

    // Too short to look for, and a search given up
    void what_cannot_be_looked_for() {
        const Phrase sung = phrase(13, 4.0);
        RecordingAlignment::MemorySource take(takeOf(sung, 5.0, 0), takeRate);
        RecordingAlignment::MemorySource recording
            (recordingOf({ { &sung, 1.0 } }, 8.0, takeRate), takeRate);
        auto m = RecordingAlignment::find(take, 1000, 1000 + 10000, recording);
        QVERIFY(!m.found);
        QVERIFY2(m.error.contains("too short"), qPrintable(m.error));

        m = RecordingAlignment::find(take, 0, 4 * 44100, recording,
                                     [](int) { return false; });
        QVERIFY(!m.found);
        QVERIFY(m.cancelled);
        QCOMPARE(m.error, QString("Cancelled"));

        RecordingAlignment::MemorySource shorter
            (recordingOf({}, 2.0, takeRate), takeRate);
        m = RecordingAlignment::find(take, 0, 4 * 44100, shorter);
        QVERIFY(!m.found);
        QVERIFY2(m.error.contains("shorter"), qPrintable(m.error));
    }
};

#endif
