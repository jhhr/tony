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

#ifndef TEST_VOICE_GATE_H
#define TEST_VOICE_GATE_H

// Tier 2: the voice threshold applied to what pYIN found: which pitch
// events are kept, how the notes are trimmed, and the levels measured
// for their stamps from a mixdown read a block at a time. Event lists
// and sample vectors in; no models and no window.

#include "../VoiceGate.h"
#include "../VoiceThreshold.h"
#include "../RealtimePitchTracker.h"

#include "TestSignals.h"

#include <QObject>
#include <QtTest>

#include <cmath>
#include <limits>
#include <map>
#include <utility>
#include <vector>

class TestVoiceGate : public QObject
{
    Q_OBJECT

    typedef sv::sv_frame_t frame_t;

    static constexpr double kRate = 44100.0;
    static const int kHop = 256;
    static const int kHalf = 1024;

    static sv::Event pitchAt(frame_t frame, float hz = 220.f) {
        return sv::Event(frame, hz, QString());
    }

    static sv::Event noteAt(frame_t frame, frame_t duration,
                            float hz = 220.f) {
        return sv::Event(frame, hz, duration, "sung");
    }

    static sv::EventVector pitchTrack(frame_t from, frame_t to) {
        sv::EventVector events;
        for (frame_t f = from; f < to; f += kHop) events.push_back(pitchAt(f));
        return events;
    }

    // Levels by stamp, given as a LevelOf that counts how often it is
    // asked; a stamp not in the table reads as silence
    struct Table {
        std::map<frame_t, double> levels;
        int asked = 0;

        VoiceGate::LevelOf levelOf() {
            return [this](frame_t stamp) {
                ++asked;
                auto i = levels.find(stamp);
                return i == levels.end() ? -200.0 : i->second;
            };
        }

        // The stamps from..to (not included), a hop apart, at one level
        void set(frame_t from, frame_t to, double level) {
            for (frame_t s = from; s < to; s += kHop) levels[s] = level;
        }
    };

    // A mixdown read as a model's getData(-1, ...) reads it, noting
    // each read
    struct Audio {
        std::vector<float> mixdown;
        std::vector<std::pair<frame_t, frame_t>> reads;

        VoiceGate::Reader reader() {
            return [this](frame_t start, frame_t count) {
                reads.push_back({ start, count });
                sv::floatvec_t got;
                for (frame_t i = std::max(frame_t(0), start);
                     i < start + count && i < frame_t(mixdown.size()); ++i) {
                    got.push_back(mixdown[size_t(i)]);
                }
                return got;
            };
        }
    };

    // The level the tracker would measure for the half window from
    // frame from, with the frames outside the audio as silence
    static double levelOfHalf(const std::vector<float> &mixdown,
                              frame_t from, int channels) {
        const frame_t n = frame_t(mixdown.size());
        if (from >= 0 && from + kHalf <= n) {
            return RealtimePitchTracker::level
                (mixdown.data() + from, kHalf, channels);
        }
        std::vector<float> half(kHalf, 0.f);
        for (frame_t i = 0; i < kHalf; ++i) {
            if (from + i >= 0 && from + i < n) {
                half[size_t(i)] = mixdown[size_t(from + i)];
            }
        }
        return RealtimePitchTracker::level(half.data(), kHalf, channels);
    }

    // The peak of a sine whose RMS is the given level
    static double peakAt(double dbfs) {
        return std::pow(10.0, dbfs / 20.0) * std::sqrt(2.0);
    }

    // A tone of 441 Hz (a whole 100 frames a period) at -23 dBFS, and
    // silence: before frame at if rising, from it if not
    static std::vector<float> toneAndSilence(frame_t at, frame_t n,
                                             bool rising) {
        std::vector<float> v =
            TestSignals::sine(441.0, kRate, int(n), peakAt(-23.0));
        for (frame_t i = 0; i < n; ++i) {
            if ((i < at) == rising) v[size_t(i)] = 0.f;
        }
        return v;
    }

    static frame_t firstFrame(const sv::EventVector &events) {
        return events.empty() ? -1 : events.front().getFrame();
    }

    static frame_t lastFrame(const sv::EventVector &events) {
        return events.empty() ? -1 : events.back().getFrame();
    }

private slots:
    // What YIN compared for a stamp begins a quarter of a block before
    // it, in either of pYIN's timings (VoiceGate::windowOffset()); Tony's
    // block is 2048 frames
    void pyin_window_offset() {
        QCOMPARE(VoiceGate::windowOffset(), 512);
        QCOMPARE(VoiceGate::windowOffset(4096), 1024);
    }

    // A pitch under the threshold goes; one at it or over it stays, as
    // it was
    void pitch_under_at_and_over_the_threshold() {
        Table table;
        table.levels = { { 512, -45.0 }, { 768, -40.0 }, { 1024, -35.0 },
                         { 1280, -200.0 } };
        const sv::EventVector pitch = { pitchAt(512, 200.f), pitchAt(768, 210.f),
                                        pitchAt(1024, 220.f), pitchAt(1280, 230.f),
                                        pitchAt(1536, 240.f) };

        VoiceGate gate(-40.0, 512);
        QVERIFY(gate.isOn());
        const sv::EventVector kept = gate.gatePitch(pitch, table.levelOf());
        QCOMPARE(int(kept.size()), 2);
        QVERIFY(kept[0] == pitch[1]);
        QVERIFY(kept[1] == pitch[2]);

        // Lower, the quieter one is kept too
        VoiceGate lower(-45.0, 512);
        QCOMPARE(int(lower.gatePitch(pitch, table.levelOf()).size()), 3);
    }

    // Off, what pYIN found is returned as it was, and no level is asked
    // for or read
    void off_changes_nothing() {
        const sv::EventVector pitch = pitchTrack(512, 8192);
        const sv::EventVector notes = { noteAt(1024, 2560), noteAt(5120, 1024) };

        for (double off : { VoiceThreshold::kOff, -80.0,
                            std::numeric_limits<double>::quiet_NaN() }) {
            VoiceGate gate(off, 512);
            QVERIFY(!gate.isOn());

            Table table;
            QVERIFY(gate.gatePitch(pitch, table.levelOf()) == pitch);
            QVERIFY(gate.gateNotes(notes, table.levelOf()) == notes);
            QCOMPARE(table.asked, 0);

            Audio audio;
            audio.mixdown = std::vector<float>(16384, 0.5f);
            QVERIFY(gate.measureLevels(gate.stampsOf(pitch, notes), 1,
                                       audio.reader()).empty());
            QVERIFY(audio.reads.empty());
        }
    }

    // A note's stamps are its onset and one each hop before its end;
    // the stamps asked for are those and the pitch events', in order,
    // each once
    void stamps_of_pitch_and_notes() {
        VoiceGate gate(-40.0, 512);
        const sv::EventVector pitch = { pitchAt(1024), pitchAt(512),
                                        pitchAt(768) };
        const sv::EventVector notes = { noteAt(1024, 768), noteAt(4000, 0),
                                        noteAt(2048, 300) };
        const std::vector<frame_t> expected =
            { 512, 768, 1024, 1280, 1536, 2048, 2304, 4000 };
        QCOMPARE(gate.stampsOf(pitch, notes), expected);
    }

    // A note with no stamp at or over the threshold goes; the others
    // stay
    void a_note_quiet_throughout_goes() {
        Table table;
        table.set(5120, 7680, -41.0);
        table.set(10240, 12800, -30.0);
        const sv::EventVector notes = { noteAt(5120, 2560),
                                        noteAt(10240, 2560, 330.f) };

        VoiceGate gate(-40.0, 512);
        const sv::EventVector kept = gate.gateNotes(notes, table.levelOf());
        QCOMPARE(int(kept.size()), 1);
        QVERIFY(kept[0] == notes[1]);
    }

    // A note quiet at both ends begins at its first voiced stamp and ends
    // a hop after its last, with its pitch and label as they were
    void a_note_is_trimmed_at_both_ends() {
        Table table;
        table.set(5120, 5632, -50.0);
        table.set(5632, 6912, -30.0);
        table.set(6912, 7680, -50.0);
        const sv::EventVector notes = { noteAt(5120, 2560, 330.f) };

        VoiceGate gate(-40.0, 512);
        const sv::EventVector kept = gate.gateNotes(notes, table.levelOf());
        QCOMPARE(int(kept.size()), 1);
        QCOMPARE(kept[0].getFrame(), frame_t(5632));
        QCOMPARE(kept[0].getDuration(), frame_t(6912 - 5632));
        QCOMPARE(kept[0].getValue(), 330.f);
        QCOMPARE(kept[0].getLabel(), QString("sung"));
    }

    // Quiet at one end only, it is trimmed at that end only
    void a_note_is_trimmed_at_one_end() {
        const sv::EventVector notes = { noteAt(5120, 2560) };
        VoiceGate gate(-40.0, 512);

        Table lateStart;
        lateStart.set(5120, 5888, -200.0);
        lateStart.set(5888, 7680, -20.0);
        sv::EventVector kept = gate.gateNotes(notes, lateStart.levelOf());
        QCOMPARE(int(kept.size()), 1);
        QCOMPARE(kept[0].getFrame(), frame_t(5888));
        QCOMPARE(kept[0].getFrame() + kept[0].getDuration(), frame_t(7680));

        Table earlyEnd;
        earlyEnd.set(5120, 5376, -40.0);
        earlyEnd.set(5376, 7680, -45.0);
        kept = gate.gateNotes(notes, earlyEnd.levelOf());
        QCOMPARE(int(kept.size()), 1);
        QCOMPARE(kept[0].getFrame(), frame_t(5120));
        QCOMPARE(kept[0].getDuration(), frame_t(kHop));
    }

    // Voiced at both ends, a note is as it was: quiet between is kept,
    // and so is an end that is not a hop after its last stamp
    void a_note_voiced_at_both_ends_is_unchanged() {
        VoiceGate gate(-40.0, 512);

        Table quietBetween;
        quietBetween.set(5120, 7680, -30.0);
        quietBetween.set(5888, 6656, -70.0);
        const sv::EventVector notes = { noteAt(5120, 2560) };
        QVERIFY(gate.gateNotes(notes, quietBetween.levelOf()) == notes);

        Table loud;
        loud.set(0, 20000, -30.0);
        const sv::EventVector odd = { noteAt(5120, 1000), noteAt(8960, 0) };
        QVERIFY(gate.gateNotes(odd, loud.levelOf()) == odd);

        // A note with no length is its onset alone
        Table quiet;
        quiet.set(0, 20000, -50.0);
        QVERIFY(gate.gateNotes({ noteAt(8960, 0) }, quiet.levelOf()).empty());
    }

    // The level of a stamp is that of the frames from the stamp less the
    // offset: a stamp just where a tone starts is heard with it at 512
    // frames (half its half is the tone) and not at 1024 (silence), and a
    // stamp 512 frames after a tone stops the other way round. So the
    // pitch kept starts, and a note is trimmed to start, 512 frames
    // later with an offset of 1024, and they end 512 frames later
    void the_window_offset() {
        const frame_t at = 8192, n = 16384;

        Audio rising, falling;
        rising.mixdown = toneAndSilence(at, n, true);
        falling.mixdown = toneAndSilence(at, n, false);

        const sv::EventVector pitch = pitchTrack(512, n);
        const sv::EventVector notes = { noteAt(at - 2048, 4096) };

        struct Case { int offset; frame_t firstRising; frame_t lastFalling; };
        for (Case c : { Case { 512, at - kHop, at + kHop },
                        Case { 1024, at + kHop, at + 3 * kHop } }) {

            VoiceGate gate(-40.0, c.offset);

            VoiceGate::Levels levels = gate.measureLevels
                (gate.stampsOf(pitch, notes), 1, rising.reader());
            sv::EventVector kept =
                gate.gatePitch(pitch, VoiceGate::lookup(levels));
            QCOMPARE(firstFrame(kept), c.firstRising);
            QCOMPARE(lastFrame(kept), pitch.back().getFrame());

            sv::EventVector trimmed =
                gate.gateNotes(notes, VoiceGate::lookup(levels));
            QCOMPARE(int(trimmed.size()), 1);
            QCOMPARE(trimmed[0].getFrame(), c.firstRising);
            QCOMPARE(trimmed[0].getFrame() + trimmed[0].getDuration(),
                     at + 2048);

            levels = gate.measureLevels
                (gate.stampsOf(pitch, notes), 1, falling.reader());
            kept = gate.gatePitch(pitch, VoiceGate::lookup(levels));
            QCOMPARE(firstFrame(kept), frame_t(512));
            QCOMPARE(lastFrame(kept), c.lastFalling);

            trimmed = gate.gateNotes(notes, VoiceGate::lookup(levels));
            QCOMPARE(int(trimmed.size()), 1);
            QCOMPARE(trimmed[0].getFrame(), at - 2048);
            QCOMPARE(trimmed[0].getFrame() + trimmed[0].getDuration(),
                     c.lastFalling + kHop);
        }
    }

    // The levels measured a block at a time are exactly those the
    // tracker's level() gives for each stamp's half window, whatever the
    // blocks, with frames outside the audio as silence; the reads are
    // never before frame 0 nor longer than a block
    void levels_are_the_trackers_measure() {
        // Two inputs, a tone on one and noise on the other, swelling and
        // fading, so that no two levels are alike
        const frame_t n = 50000;
        const std::vector<float> tone =
            TestSignals::sine(441.0, kRate, int(n), 0.3);
        const std::vector<float> noise = TestSignals::whiteNoise(int(n), 5, 0.1);
        Audio audio;
        audio.mixdown.resize(size_t(n));
        for (frame_t i = 0; i < n; ++i) {
            double swell = std::sin(TestSignals::kPi * double(i) / double(n));
            audio.mixdown[size_t(i)] =
                float(swell * (tone[size_t(i)] + noise[size_t(i)]));
        }

        // From the very start, whose half begins before frame 0, to past
        // the end
        std::vector<frame_t> stamps;
        for (frame_t s = 0; s < n + 2048; s += kHop) stamps.push_back(s);

        for (int offset : { 512, 1024 }) {
            for (frame_t readFrames : { frame_t(100), frame_t(1024),
                                        frame_t(1500), frame_t(4096),
                                        VoiceGate::kReadFrames }) {
                VoiceGate gate(-50.0, offset);
                audio.reads.clear();
                const VoiceGate::Levels levels =
                    gate.measureLevels(stamps, 2, audio.reader(), readFrames);

                QCOMPARE(int(levels.size()), int(stamps.size()));
                for (frame_t s : stamps) {
                    auto i = levels.find(s);
                    QVERIFY(i != levels.end());
                    const double expected =
                        levelOfHalf(audio.mixdown, s - offset, 2);
                    QVERIFY2(i->second == expected,
                             qPrintable(QString("stamp %1 at offset %2, "
                                                "blocks of %3: %4 dBFS, "
                                                "expected %5")
                                        .arg(s).arg(offset).arg(readFrames)
                                        .arg(i->second).arg(expected)));
                }

                // Wholly past the end is silence
                QCOMPARE(levels.rbegin()->second, -200.0);

                const frame_t most = std::max(readFrames, frame_t(kHalf));
                for (const auto &r : audio.reads) {
                    QVERIFY2(r.first >= 0 && r.second <= most,
                             qPrintable(QString("read %1 frames from %2")
                                        .arg(r.second).arg(r.first)));
                }
            }
        }

        // In any order, the same
        VoiceGate gate(-50.0, 512);
        const VoiceGate::Levels inOrder =
            gate.measureLevels(stamps, 2, audio.reader(), 4096);
        std::vector<frame_t> reversed(stamps.rbegin(), stamps.rend());
        QVERIFY(gate.measureLevels(reversed, 2, audio.reader(), 4096) ==
                inOrder);
    }

    // Stamps far apart are read a block each, and nothing between them
    void only_the_stamps_are_read() {
        Audio audio;
        audio.mixdown = std::vector<float>(1000000, 0.25f);
        VoiceGate gate(-40.0, 512);
        const VoiceGate::Levels levels = gate.measureLevels
            ({ 1024, 1280, 1536, 900000 }, 1, audio.reader(), 4096);
        QCOMPARE(int(levels.size()), 4);
        QCOMPARE(int(audio.reads.size()), 2);

        // Looked up, a stamp not measured is silence
        const VoiceGate::LevelOf levelOf = VoiceGate::lookup(levels);
        QCOMPARE(levelOf(1024), levels.at(1024));
        QVERIFY(levelOf(1024) > -13.0);
        QCOMPARE(levelOf(2048), -200.0);
        QCOMPARE(audio.reads[0].first, frame_t(1024 - 512));
        QCOMPARE(audio.reads[1].first, frame_t(900000 - 512));
    }
};

#endif
