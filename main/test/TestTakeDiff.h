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

#ifndef TEST_TAKE_DIFF_H
#define TEST_TAKE_DIFF_H

// Tier 2: the comparisons the development checks make on a real take,
// on synthetic samples and events: each passing, and failing on
// purpose. Two tests run the real splice through files, since where
// its fades fall is what the audio comparison relies on.

#include "../TakeDiff.h"
#include "../TakeAudio.h"
#include "TestSignals.h"

#include "data/fileio/FileSource.h"
#include "data/fileio/WavFileReader.h"
#include "data/fileio/WavFileWriter.h"

#include <QObject>
#include <QtTest>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

class TestTakeDiff : public QObject
{
    Q_OBJECT

    typedef sv::sv_frame_t frame_t;
    typedef Coverage::Range Range;
    typedef std::vector<float> Signal; // interleaved

    static constexpr double kRate = 44100.0;
    static constexpr float kAmplitude = 0.25f; // -12 dBFS, as the reference

    // 220.5 Hz: exactly 200 samples a period at 44.1 kHz, so a phase
    // is a frame and nothing drifts
    static constexpr double kToneHz = 220.5;

    QTemporaryDir m_dir;
    int m_fileCounter = 0;

    static frame_t framesOf(double seconds) {
        return frame_t(std::llround(seconds * kRate));
    }

    // A sine whose phase at frame 0 is "phase" radians
    static Signal sine(frame_t frames, double phase = 0.0) {
        Signal s(frames);
        for (frame_t i = 0; i < frames; ++i) {
            s[i] = kAmplitude * float(std::sin(2.0 * TestSignals::kPi * kToneHz
                                               * double(i) / kRate + phase));
        }
        return s;
    }

    // Distinct and nowhere silent, so that any change shows
    static Signal ramp(frame_t frames, int channels = 1) {
        Signal s(frames * channels);
        for (frame_t i = 0; i < frames * channels; ++i) {
            s[i] = 0.1f + 0.5f * float(i) / float(frames * channels);
        }
        return s;
    }

    // Gaussian, from a generator of our own so that every platform
    // makes the same noise
    static Signal noise(frame_t frames, float sd) {
        Signal s(frames);
        uint32_t state = 12345;
        auto uniform = [&]() {
            state = state * 1664525u + 1013904223u;
            return (double(state) + 1.0) / 4294967297.0;
        };
        for (frame_t i = 0; i < frames; ++i) {
            double u = uniform(), v = uniform();
            s[i] = sd * float(std::sqrt(-2.0 * std::log(u)) *
                              std::cos(2.0 * TestSignals::kPi * v));
        }
        return s;
    }

    static sv::Event pitchAt(frame_t frame, float hz = 220.f) {
        return sv::Event(frame, hz, QString());
    }

    static sv::Event noteAt(frame_t frame, frame_t duration,
                            float hz = 220.f) {
        return sv::Event(frame, hz, duration, "sung");
    }

    // An event every hop from "from" to "to", as pYIN leaves over a
    // held tone
    static sv::EventVector pitchTrack(frame_t from, frame_t to) {
        sv::EventVector events;
        for (frame_t f = from; f < to; f += TakeDiff::kHopFrames) {
            events.push_back(pitchAt(f));
        }
        return events;
    }

    static void removeFrame(sv::EventVector &events, frame_t frame) {
        events.erase(std::remove_if(events.begin(), events.end(),
                                    [&](const sv::Event &e) {
                                        return e.getFrame() == frame;
                                    }), events.end());
    }

    QString writeWav(const Signal &samples) {
        QString path = m_dir.filePath
            (QString("source-%1.wav").arg(++m_fileCounter));
        sv::WavFileWriter writer(path, kRate, 1,
                                 sv::WavFileWriter::WriteToTarget);
        sv::floatvec_t data(samples.begin(), samples.end());
        if (!writer.isOK() || !writer.putInterleavedFrames(data) ||
            !writer.close()) {
            return "";
        }
        return path;
    }

    QString newPath() {
        return m_dir.filePath(QString("take-%1.wav").arg(++m_fileCounter));
    }

    // All of a mono file, as a check reads a take's file
    static Signal read(QString path) {
        sv::WavFileReader reader { sv::FileSource(path) };
        if (!reader.isOK() || reader.getChannelCount() != 1) return {};
        auto data = reader.getInterleavedFrames(0, reader.getFrameCount());
        return Signal(data.begin(), data.end());
    }

    static TakeDiff::AudioDiff compare(const Signal &before,
                                       const Signal &after, Range range,
                                       int channels = 1) {
        return TakeDiff::audioOutside
            (before.data(), frame_t(before.size()) / channels,
             after.data(), frame_t(after.size()) / channels,
             channels, range);
    }

    static TakeDiff::SampleStep stepAt(const Signal &samples, frame_t join,
                                       int channels = 1) {
        return TakeDiff::stepAt(samples.data(),
                                frame_t(samples.size()) / channels,
                                channels, kRate, join);
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    // ---- Audio outside a range ----

    // Everything in the range may change, its first and last frames too
    void audio_changed_only_inside_passes() {
        Signal before = ramp(10000);
        Signal after = before;
        for (frame_t i = 4000; i < 6000; ++i) after[i] = 0.9f;

        TakeDiff::AudioDiff d = compare(before, after, Range(4000, 6000));
        QVERIFY(d.pass);
        QCOMPARE(d.firstDifference, frame_t(-1));
        QCOMPARE(d.differences, frame_t(0));
    }

    // Bit for bit: the smallest change a float can make, one frame
    // outside the range at either end, or in one channel only
    void audio_changed_just_outside_fails() {
        Signal before = ramp(10000);

        Signal after = before;
        after[6000] = std::nextafter(after[6000], 1.f);
        TakeDiff::AudioDiff d = compare(before, after, Range(4000, 6000));
        QVERIFY(!d.pass);
        QCOMPARE(d.firstDifference, frame_t(6000));
        QCOMPARE(d.differences, frame_t(1));
        QVERIFY(d.largestDifference > 0.0);
        QVERIFY(d.largestDifference < 1e-6);

        after = before;
        after[3999] = 0.f;
        d = compare(before, after, Range(4000, 6000));
        QVERIFY(!d.pass);
        QCOMPARE(d.firstDifference, frame_t(3999));

        Signal stereo = ramp(10000, 2);
        after = stereo;
        after[6000 * 2 + 1] = 0.f;
        d = compare(stereo, after, Range(4000, 6000), 2);
        QVERIFY(!d.pass);
        QCOMPARE(d.firstDifference, frame_t(6000));
        QCOMPARE(d.differences, frame_t(1));
    }

    // A take is silent wherever nothing was recorded, in the file or
    // past its end
    void audio_past_the_end_is_silence() {
        Signal before = ramp(5000);

        // A punch-in after the old end makes the file longer
        Signal after = before;
        after.resize(8000, 0.f);
        for (frame_t i = 6000; i < 8000; ++i) after[i] = 0.9f;
        QVERIFY(compare(before, after, Range(6000, 8000)).pass);

        after[5500] = 0.1f;
        TakeDiff::AudioDiff d = compare(before, after, Range(6000, 8000));
        QVERIFY(!d.pass);
        QCOMPARE(d.firstDifference, frame_t(5500));

        // Sound lost off the end is a difference too
        after = before;
        after.resize(4500);
        d = compare(before, after, Range(1000, 2000));
        QVERIFY(!d.pass);
        QCOMPARE(d.firstDifference, frame_t(4500));
        QCOMPARE(d.differences, frame_t(500));
    }

    // The real splice and erase, through their files: bit-identical
    // outside the range they fill, and their fades reach right to its
    // ends, so that the range less one frame at either end differs
    // exactly there. Were a fade to fall outside the range, the first
    // comparison would fail
    void audio_splice_and_erase_fades_lie_inside_the_range() {
        QString oldPath = writeWav(sine(20000));
        QString recording = writeWav(Signal(6000, -0.2f));
        QVERIFY(oldPath != "" && recording != "");

        QString spliced = newPath();
        Range placed;
        QCOMPARE(TakeAudio::splice(oldPath, recording, 0, 5000, 4000,
                                   spliced, &placed), QString());
        QCOMPARE(placed.start, frame_t(5000));
        QCOMPARE(placed.end, frame_t(9000));

        Signal before = read(oldPath), after = read(spliced);
        QCOMPARE(frame_t(before.size()), frame_t(20000));
        QCOMPARE(frame_t(after.size()), frame_t(20000));

        TakeDiff::AudioDiff d = compare(before, after, placed);
        QVERIFY2(d.pass, qPrintable(QString("differs from frame %1")
                                    .arg(d.firstDifference)));

        d = compare(before, after, Range(placed.start + 1, placed.end));
        QVERIFY(!d.pass);
        QCOMPARE(d.firstDifference, placed.start);
        QCOMPARE(d.differences, frame_t(1));

        d = compare(before, after, Range(placed.start, placed.end - 1));
        QVERIFY(!d.pass);
        QCOMPARE(d.firstDifference, placed.end - 1);

        QString erased = newPath();
        Range gone(12050, 13050); // from a peak: a fade there changes it
        QCOMPARE(TakeAudio::erase(spliced, Coverage::Ranges { gone }, erased),
                 QString());
        Signal later = read(erased);
        QVERIFY(compare(after, later, gone).pass);
        d = compare(after, later, Range(gone.start + 1, gone.end));
        QCOMPARE(d.firstDifference, gone.start);
    }

    // ---- Events outside a range, give or take the margin ----

    void events_changed_inside_the_margin_pass() {
        Range range(100000, 200000);
        frame_t margin = framesOf(TakeDiff::kEventMarginSeconds);

        sv::EventVector before = pitchTrack(0, 300000);
        sv::EventVector after;
        for (const sv::Event &e : before) {
            bool inside = e.getFrame() >= range.start - margin &&
                e.getFrame() < range.end + margin;
            after.push_back(inside ? e.withValue(330.f) : e);
        }
        after.push_back(pitchAt(range.start + 7)); // one more, inside

        TakeDiff::EventDiff d =
            TakeDiff::eventsOutside(before, after, range, kRate);
        QVERIFY(d.pass);
        QCOMPARE(d.firstDifference, frame_t(-1));
        QCOMPARE(d.window.start, range.start - margin);
        QCOMPARE(d.window.end, range.end + margin);

        // A note that crosses the window's edge counts as inside: cut
        // back, as the merge may cut an old note a new one overlaps
        sv::EventVector notes { noteAt(50000, 30000), noteAt(80000, 20000),
                                noteAt(150000, 10000), noteAt(230000, 10000) };
        sv::EventVector cut { noteAt(50000, 30000), noteAt(80000, 10000),
                              noteAt(160000, 5000), noteAt(230000, 10000) };
        QVERIFY(TakeDiff::eventsOutside(notes, cut, range, kRate).pass);
    }

    // The window is [start - margin, end + margin), to the frame
    void pitch_changed_just_outside_the_margin_fails() {
        Range range(100000, 200000);
        frame_t margin = framesOf(TakeDiff::kEventMarginSeconds);
        frame_t first = range.start - margin, last = range.end + margin;

        sv::EventVector before { pitchAt(first - 1), pitchAt(first),
                                 pitchAt(last - 1), pitchAt(last) };

        sv::EventVector after { pitchAt(first - 1, 221.f), pitchAt(first, 300.f),
                                pitchAt(last - 1, 300.f), pitchAt(last) };
        TakeDiff::EventDiff d =
            TakeDiff::eventsOutside(before, after, range, kRate);
        QVERIFY(!d.pass);
        QCOMPARE(int(d.changed.size()), 1);
        QCOMPARE(d.changed[0].first.getFrame(), first - 1);
        QCOMPARE(d.changed[0].second.getValue(), 221.f);
        QCOMPARE(d.firstDifference, first - 1);

        after = { pitchAt(first - 1), pitchAt(first), pitchAt(last - 1),
                  pitchAt(last, 221.f) };
        d = TakeDiff::eventsOutside(before, after, range, kRate);
        QVERIFY(!d.pass);
        QCOMPARE(d.firstDifference, last);
    }

    void events_added_removed_and_changed_are_told_apart() {
        Range range(100000, 110000);
        sv::EventVector before { pitchAt(1000), pitchAt(2000), pitchAt(3000) };
        sv::EventVector after { pitchAt(4000), pitchAt(2000, 230.f),
                                pitchAt(1000) }; // and out of order

        TakeDiff::EventDiff d =
            TakeDiff::eventsOutside(before, after, range, kRate);
        QVERIFY(!d.pass);
        QCOMPARE(int(d.removed.size()), 1);
        QCOMPARE(d.removed[0].getFrame(), frame_t(3000));
        QCOMPARE(int(d.added.size()), 1);
        QCOMPARE(d.added[0].getFrame(), frame_t(4000));
        QCOMPARE(int(d.changed.size()), 1);
        QCOMPARE(d.changed[0].first.getValue(), 220.f);
        QCOMPARE(d.changed[0].second.getValue(), 230.f);
        QCOMPARE(d.firstDifference, frame_t(2000));

        // The same event twice on one side is one more event there
        before = { pitchAt(1000), pitchAt(1000) };
        after = { pitchAt(1000) };
        d = TakeDiff::eventsOutside(before, after, range, kRate);
        QVERIFY(!d.pass);
        QCOMPARE(int(d.removed.size()), 1);
        QVERIFY(d.changed.empty());
    }

    void note_changed_outside_the_margin_fails() {
        Range range(100000, 200000);

        // Shorter, wholly outside
        sv::EventVector before { noteAt(20000, 5000) };
        sv::EventVector after { noteAt(20000, 4000) };
        TakeDiff::EventDiff d =
            TakeDiff::eventsOutside(before, after, range, kRate);
        QVERIFY(!d.pass);
        QCOMPARE(int(d.changed.size()), 1);
        QCOMPARE(d.changed[0].second.getDuration(), frame_t(4000));

        // Grown into the window: the note that was outside is gone
        before = { noteAt(80000, 8000) };
        after = { noteAt(80000, 10000) };
        d = TakeDiff::eventsOutside(before, after, range, kRate);
        QVERIFY(!d.pass);
        QCOMPARE(int(d.removed.size()), 1);
        QVERIFY(d.added.empty());
    }

    // ---- Pitch across a join ----

    void pitch_continuous_across_a_join_passes() {
        frame_t join = framesOf(2.0) + 100;
        sv::EventVector pitch = pitchTrack(0, framesOf(5.0));

        TakeDiff::PitchJoin p = TakeDiff::pitchAcross(pitch, join, kRate);
        QVERIFY(p.pass);
        QCOMPARE(p.largestGap, TakeDiff::kHopFrames);
        QCOMPARE(p.doubled, 0);
        QCOMPARE(p.outOfOrder, 0);

        frame_t reach = framesOf(TakeDiff::kPitchWindowSeconds);
        QCOMPARE(p.window.start, join - reach);
        QCOMPARE(p.window.end, join + reach);
        int expected = 0;
        for (const sv::Event &e : pitch) {
            if (e.getFrame() >= join - reach && e.getFrame() < join + reach) {
                ++expected;
            }
        }
        QCOMPARE(p.events, expected);
    }

    void pitch_event_dropped_at_the_join_fails() {
        frame_t join = framesOf(2.0) + 100;
        frame_t atJoin = (join / TakeDiff::kHopFrames) * TakeDiff::kHopFrames;
        sv::EventVector pitch = pitchTrack(0, framesOf(5.0));
        removeFrame(pitch, atJoin);

        TakeDiff::PitchJoin p = TakeDiff::pitchAcross(pitch, join, kRate);
        QVERIFY(!p.pass);
        QCOMPARE(p.largestGap, 2 * TakeDiff::kHopFrames);
        QCOMPARE(p.largestGapFrom, atJoin - TakeDiff::kHopFrames);

        // Two hops are allowed when asked for
        QVERIFY(TakeDiff::pitchAcross(pitch, join, kRate,
                                      TakeDiff::kPitchWindowSeconds, 2).pass);
    }

    void pitch_doubled_frame_fails() {
        frame_t join = framesOf(2.0) + 100;
        frame_t atJoin = (join / TakeDiff::kHopFrames) * TakeDiff::kHopFrames;
        sv::EventVector pitch = pitchTrack(0, framesOf(5.0));

        sv::EventVector doubled = pitch;
        auto at = std::find(doubled.begin(), doubled.end(), pitchAt(atJoin));
        QVERIFY(at != doubled.end());
        doubled.insert(at + 1, pitchAt(atJoin, 221.f));

        TakeDiff::PitchJoin p = TakeDiff::pitchAcross(doubled, join, kRate);
        QVERIFY(!p.pass);
        QCOMPARE(p.doubled, 1);
        QCOMPARE(p.firstDoubled, atJoin);
        QCOMPARE(p.outOfOrder, 0);
        QCOMPARE(p.largestGap, TakeDiff::kHopFrames);

        // Two neighbours the wrong way round
        sv::EventVector swapped = pitch;
        at = std::find(swapped.begin(), swapped.end(), pitchAt(atJoin));
        std::iter_swap(at, at + 1);
        p = TakeDiff::pitchAcross(swapped, join, kRate);
        QVERIFY(!p.pass);
        QCOMPARE(p.outOfOrder, 1);
        QCOMPARE(p.firstOutOfOrder, atJoin);
        QCOMPARE(p.doubled, 0);
    }

    // Only the window counts, but a hole that reaches into it does
    void pitch_looks_only_around_the_join() {
        const frame_t hop = TakeDiff::kHopFrames;
        frame_t join = framesOf(2.0) + 100;
        frame_t atJoin = (join / hop) * hop;
        sv::EventVector pitch = pitchTrack(0, framesOf(5.0));

        // A doubled frame and a hole a second away: pYIN's own doubled
        // frame near the end of a run is like this
        sv::EventVector far = pitch;
        far.push_back(pitchAt(atJoin + 172 * hop));
        for (int k = 1; k <= 10; ++k) removeFrame(far, atJoin - 172 * hop + k * hop);
        QVERIFY(TakeDiff::pitchAcross(far, join, kRate).pass);

        // The merge's seam, a quarter of a second before the join
        sv::EventVector seam = pitch;
        frame_t seamFrame = ((join - framesOf(0.25)) / hop) * hop;
        removeFrame(seam, seamFrame);
        QVERIFY(!TakeDiff::pitchAcross(seam, join, kRate).pass);

        // A hole from before the window into it
        frame_t windowStart = join - framesOf(TakeDiff::kPitchWindowSeconds);
        sv::EventVector into = pitch;
        into.erase(std::remove_if(into.begin(), into.end(),
                                  [&](const sv::Event &e) {
                                      return e.getFrame() > windowStart - 2000 &&
                                          e.getFrame() < windowStart + 1000;
                                  }), into.end());
        TakeDiff::PitchJoin p = TakeDiff::pitchAcross(into, join, kRate);
        QVERIFY(!p.pass);
        QVERIFY(p.largestGapFrom < windowStart);

        // The track stops inside the window
        sv::EventVector stops = pitchTrack(0, join + framesOf(0.2));
        p = TakeDiff::pitchAcross(stops, join, kRate);
        QVERIFY(!p.pass);
        QVERIFY(p.largestGap > framesOf(0.2));

        // Nothing at all
        p = TakeDiff::pitchAcross(sv::EventVector(), join, kRate);
        QVERIFY(!p.pass);
        QCOMPARE(p.events, 0);
        QCOMPARE(p.largestGap, 2 * framesOf(TakeDiff::kPitchWindowSeconds));
    }

    // ---- One note across a join ----

    void one_note_across_a_join_passes() {
        frame_t join = framesOf(10.0);
        sv::EventVector notes { noteAt(framesOf(5.0), framesOf(1.0)),
                                noteAt(join - framesOf(1.5), framesOf(3.0)),
                                noteAt(framesOf(13.0), framesOf(1.0)) };

        TakeDiff::NoteJoin n = TakeDiff::notesAcross(notes, join, kRate);
        QVERIFY(n.pass);
        QCOMPARE(int(n.spanning.size()), 1);
        QCOMPARE(n.spanning[0].getFrame(), join - framesOf(1.5));
        QVERIFY(n.edgesNear.empty());
        QCOMPARE(n.nearestEdge, -framesOf(1.5));
    }

    void note_split_at_the_join_fails() {
        frame_t join = framesOf(10.0);
        frame_t a = join - framesOf(1.5), b = join + framesOf(1.5);
        frame_t clearance = framesOf(TakeDiff::kNoteClearanceSeconds);

        // Split at the join: the first ends there, so only the second
        // holds it
        sv::EventVector notes { noteAt(a, join - a), noteAt(join, b - join) };
        TakeDiff::NoteJoin n = TakeDiff::notesAcross(notes, join, kRate);
        QVERIFY(!n.pass);
        QCOMPARE(int(n.spanning.size()), 1);
        QCOMPARE(n.spanning[0].getFrame(), join);
        QCOMPARE(int(n.edgesNear.size()), 2);
        QCOMPARE(n.nearestEdge, frame_t(0));

        // At the merge's seam
        frame_t seam = join - framesOf(0.25);
        notes = { noteAt(a, seam - a), noteAt(seam, b - seam) };
        n = TakeDiff::notesAcross(notes, join, kRate);
        QVERIFY(!n.pass);
        QCOMPARE(n.nearestEdge, -framesOf(0.25));

        // At the clearance, and one frame beyond it
        frame_t split = join - clearance;
        notes = { noteAt(a, split - a), noteAt(split, b - split) };
        QVERIFY(!TakeDiff::notesAcross(notes, join, kRate).pass);
        split = join - clearance - 1;
        notes = { noteAt(a, split - a), noteAt(split, b - split) };
        QVERIFY(TakeDiff::notesAcross(notes, join, kRate).pass);

        // None, or two at once
        QVERIFY(!TakeDiff::notesAcross(sv::EventVector(), join, kRate).pass);
        notes = { noteAt(a, b - a), noteAt(a, b - a, 330.f) };
        n = TakeDiff::notesAcross(notes, join, kRate);
        QVERIFY(!n.pass);
        QCOMPARE(int(n.spanning.size()), 2);
    }

    // ---- No step in the samples at a join ----

    void a_steady_tone_has_no_step() {
        Signal tone = sine(framesOf(0.2));
        TakeDiff::SampleStep s = stepAt(tone, framesOf(0.1));
        QVERIFY2(s.pass && std::fabs(s.stepDb) < 0.1,
                 qPrintable(QString("%1 dB").arg(s.stepDb)));

        // Silence has no step either
        s = stepAt(Signal(framesOf(0.2), 0.f), framesOf(0.1));
        QVERIFY(s.pass);
        QCOMPARE(s.stepDb, 0.0);
    }

    // The threshold's own reasoning: noise reads a few dB, well under it
    void white_noise_has_no_step() {
        Signal hiss = noise(framesOf(0.2), 0.05f);
        TakeDiff::SampleStep s = stepAt(hiss, framesOf(0.1));
        QVERIFY2(s.pass && s.stepDb > 0.0 && s.stepDb < 7.0,
                 qPrintable(QString("%1 dB").arg(s.stepDb)));
    }

    // The tone at its peak is cut to the same tone at its trough
    void a_hard_cut_in_a_sine_is_a_step() {
        frame_t join = 4050; // a quarter period past a whole one: the peak
        Signal cut = sine(framesOf(0.2));
        Signal opposite = sine(framesOf(0.2), TestSignals::kPi);
        std::copy(opposite.begin() + join, opposite.end(), cut.begin() + join);

        TakeDiff::SampleStep s = stepAt(cut, join);
        QVERIFY(!s.pass);
        QVERIFY2(s.stepDb > 30.0, qPrintable(QString("%1 dB").arg(s.stepDb)));
        QCOMPARE(s.largestAt, join);
        QVERIFY(s.largest > 1.9 * kAmplitude);

        // Seen in the one channel that has it
        Signal stereo(cut.size() * 2);
        Signal tone = sine(framesOf(0.2));
        for (size_t i = 0; i < cut.size(); ++i) {
            stereo[i * 2] = tone[i];
            stereo[i * 2 + 1] = cut[i];
        }
        s = stepAt(stereo, join, 2);
        QVERIFY(!s.pass);
        QCOMPARE(s.channel, 1);
    }

    // The real splice, through its files: a recording of the opposite
    // phase crossfaded in reads no step, and cut in without its fade
    // reads one
    void the_splice_crossfade_leaves_no_step() {
        QString oldPath = writeWav(sine(20000));
        QString recording = writeWav(sine(6000, -TestSignals::kPi / 2));
        QVERIFY(oldPath != "" && recording != "");

        frame_t position = 10050; // the old take at its peak
        QString faded = newPath(), hard = newPath();
        QCOMPARE(TakeAudio::splice(oldPath, recording, 0, position, 4000,
                                   faded), QString());
        QCOMPARE(TakeAudio::splice(oldPath, recording, 0, position, 4000,
                                   hard, nullptr, 0), QString());

        TakeDiff::SampleStep s = stepAt(read(faded), position);
        QVERIFY2(s.pass && s.stepDb < 1.0,
                 qPrintable(QString("%1 dB").arg(s.stepDb)));
        s = stepAt(read(faded), position + 4000);
        QVERIFY2(s.pass, qPrintable(QString("%1 dB").arg(s.stepDb)));

        s = stepAt(read(hard), position);
        QVERIFY2(!s.pass && s.stepDb > 30.0,
                 qPrintable(QString("%1 dB").arg(s.stepDb)));
        QCOMPARE(s.largestAt, position);
    }

    void a_join_outside_the_audio_does_not_pass() {
        Signal tone = sine(framesOf(0.2));
        QVERIFY(!stepAt(tone, framesOf(0.2) + 1000).pass);
        QVERIFY(!stepAt(Signal(), 0).pass);
    }
};

#endif
