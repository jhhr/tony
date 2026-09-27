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

#ifndef TEST_TAKE_AUDIO_H
#define TEST_TAKE_AUDIO_H

// Tier 2: putting a recording into the audio file of a singing take,
// and taking parts of one out again. The signals are constants and
// ramps so that every sample of the result says where it came from.

#include "../TakeAudio.h"

#include "TestSignals.h"

#include "data/fileio/FileSource.h"
#include "data/fileio/WavFileReader.h"
#include "data/fileio/WavFileWriter.h"

#include <QObject>
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>

#include <cmath>
#include <vector>

class TestTakeAudio : public QObject
{
    Q_OBJECT

    typedef sv::sv_frame_t frame_t;
    typedef std::vector<float> Signal; // interleaved

    static constexpr double kRate = 44100.0;
    static constexpr float kTolerance = 1e-3f; // the files may be 16-bit
    static constexpr frame_t kFade = 100;

    QTemporaryDir m_dir;
    int m_fileCounter = 0;

    QString newPath(QString stem = "audio") {
        return m_dir.filePath(QString("%1-%2.wav").arg(stem).arg(++m_fileCounter));
    }

    QString writeWav(const Signal &interleaved, int channels = 1,
                     double rate = kRate) {
        QString path = newPath("source");
        sv::WavFileWriter writer(path, rate, channels,
                                 sv::WavFileWriter::WriteToTarget);
        sv::floatvec_t data(interleaved.begin(), interleaved.end());
        if (!writer.isOK() || !writer.putInterleavedFrames(data) ||
            !writer.close()) {
            return "";
        }
        return path;
    }

    static Signal constant(frame_t frames, float value) {
        return Signal(frames, value);
    }

    // Rises from 0.1 to 0.6, so that no frame of it is silent and each
    // differs from the next by more than the tolerance only slowly:
    // value says which frame, to within a few
    static Signal ramp(frame_t frames) {
        Signal s(frames);
        for (frame_t i = 0; i < frames; ++i) s[i] = rampValue(i, frames);
        return s;
    }
    static float rampValue(frame_t i, frame_t frames) {
        return 0.1f + 0.5f * float(i) / float(frames);
    }

    struct Audio {
        bool ok = false;
        int channels = 0;
        frame_t frames = 0;
        Signal samples;
        float at(frame_t i, int c = 0) const { return samples[i * channels + c]; }
    };

    Audio read(QString path) {
        Audio a;
        sv::WavFileReader reader { sv::FileSource(path) };
        if (!reader.isOK()) return a;
        a.ok = true;
        a.channels = reader.getChannelCount();
        a.frames = reader.getFrameCount();
        auto data = reader.getInterleavedFrames(0, a.frames);
        a.samples.assign(data.begin(), data.end());
        return a;
    }

    static bool nearly(float a, float b) { return std::fabs(a - b) <= kTolerance; }

    // Every frame of [from, to) in channel c is value
    static bool allNearly(const Audio &a, frame_t from, frame_t to, float value,
                          int c = 0) {
        for (frame_t i = from; i < to; ++i) {
            if (!nearly(a.at(i, c), value)) {
                qWarning("frame %lld is %f, expected %f",
                         (long long)i, a.at(i, c), value);
                return false;
            }
        }
        return true;
    }

    // [from, to) goes from one value to the other without ever
    // leaving the span between them or turning back
    static bool monotonicBetween(const Audio &a, frame_t from, frame_t to,
                                 float first, float last) {
        float lo = std::min(first, last) - kTolerance;
        float hi = std::max(first, last) + kTolerance;
        float direction = (last > first ? 1.f : -1.f);
        for (frame_t i = from; i < to; ++i) {
            float v = a.at(i);
            if (v < lo || v > hi) {
                qWarning("frame %lld is %f, outside %f..%f",
                         (long long)i, v, lo, hi);
                return false;
            }
            if (i > from && (v - a.at(i - 1)) * direction < -kTolerance) {
                qWarning("frame %lld turns back", (long long)i);
                return false;
            }
        }
        return true;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void fade_length() {
        QCOMPARE(TakeAudio::defaultFadeFrames(44100.0), frame_t(220));
        QCOMPARE(TakeAudio::defaultFadeFrames(48000.0), frame_t(240));
    }

    // The first recording of a take, started part of the way in
    void splice_into_nothing() {
        QString recording = writeWav(constant(4000, 0.5f));
        QString out = newPath();
        Coverage::Range placed;
        QString error = TakeAudio::splice("", recording, 0, 1000, -1, out,
                                          &placed, kFade);
        QCOMPARE(error, QString());
        QCOMPARE(placed, Coverage::Range(1000, 5000));

        Audio a = read(out);
        QVERIFY(a.ok);
        QCOMPARE(a.channels, 1);
        QCOMPARE(a.frames, frame_t(5000));
        QVERIFY(allNearly(a, 0, 1000, 0.f));
        QVERIFY(monotonicBetween(a, 1000, 1000 + kFade, 0.f, 0.5f));
        QVERIFY(allNearly(a, 1000 + kFade, 5000 - kFade, 0.5f));
        QVERIFY(monotonicBetween(a, 5000 - kFade, 5000, 0.5f, 0.f));
        // and it does fade: the first frame is nearly silent
        QVERIFY(a.at(1000) < 0.05f);
        QVERIFY(a.at(4999) < 0.05f);
    }

    // What was recorded before the singer could have heard the
    // reference is left out: frame "offset" of the recording is what
    // lands on the position
    void splice_removes_latency() {
        const frame_t n = 8000, offset = 1500, position = 3000;
        QString recording = writeWav(ramp(n));
        QString out = newPath();
        Coverage::Range placed;
        QCOMPARE(TakeAudio::splice("", recording, offset, position, -1, out,
                                   &placed, kFade), QString());
        QCOMPARE(placed, Coverage::Range(position, position + n - offset));

        Audio a = read(out);
        QVERIFY(a.ok);
        QCOMPARE(a.frames, position + n - offset);
        for (frame_t k : { frame_t(kFade), frame_t(2000), n - offset - kFade - 1 }) {
            QVERIFY2(nearly(a.at(position + k), rampValue(offset + k, n)),
                     qPrintable(QString::number(k)));
        }
    }

    void splice_length_limits_what_is_used() {
        QString recording = writeWav(constant(4000, 0.5f));
        QString out = newPath();
        Coverage::Range placed;
        QCOMPARE(TakeAudio::splice("", recording, 500, 0, 1000, out,
                                   &placed, kFade), QString());
        QCOMPARE(placed, Coverage::Range(0, 1000));
        QCOMPARE(read(out).frames, frame_t(1000));

        // more than there is: what there is
        out = newPath();
        QCOMPARE(TakeAudio::splice("", recording, 500, 0, 99999, out,
                                   &placed, kFade), QString());
        QCOMPARE(placed, Coverage::Range(0, 3500));
    }

    // Singing one part again
    void splice_replaces_the_middle() {
        QString old = writeWav(constant(10000, 0.25f));
        QString recording = writeWav(constant(2000, -0.5f));
        QString out = newPath();
        QCOMPARE(TakeAudio::splice(old, recording, 0, 3000, -1, out,
                                   nullptr, kFade), QString());

        Audio a = read(out);
        QVERIFY(a.ok);
        QCOMPARE(a.frames, frame_t(10000));
        QVERIFY(allNearly(a, 0, 3000, 0.25f));
        // crossfaded: straight from the one to the other, with no dip
        // to silence on the way and no jump
        QVERIFY(monotonicBetween(a, 3000, 3000 + kFade, 0.25f, -0.5f));
        QVERIFY(allNearly(a, 3000 + kFade, 5000 - kFade, -0.5f));
        QVERIFY(monotonicBetween(a, 5000 - kFade, 5000, -0.5f, 0.25f));
        QVERIFY(allNearly(a, 5000, 10000, 0.25f));
        for (frame_t i = 2999; i < 5001; ++i) {
            QVERIFY2(std::fabs(a.at(i + 1) - a.at(i)) < 0.8f / float(kFade),
                     qPrintable(QString("jump at %1").arg(i)));
        }

        // and the take it was made from is as it was
        Audio before = read(old);
        QCOMPARE(before.frames, frame_t(10000));
        QVERIFY(allNearly(before, 0, 10000, 0.25f));
    }

    void splice_past_the_end_lengthens() {
        QString old = writeWav(constant(3000, 0.25f));
        QString recording = writeWav(constant(2000, 0.5f));

        // running off the end
        QString out = newPath();
        QCOMPARE(TakeAudio::splice(old, recording, 0, 2000, -1, out,
                                   nullptr, kFade), QString());
        Audio a = read(out);
        QCOMPARE(a.frames, frame_t(4000));
        QVERIFY(allNearly(a, 0, 2000, 0.25f));
        QVERIFY(allNearly(a, 2000 + kFade, 4000 - kFade, 0.5f));

        // starting beyond it, after an intermission
        out = newPath();
        QCOMPARE(TakeAudio::splice(old, recording, 0, 6000, -1, out,
                                   nullptr, kFade), QString());
        a = read(out);
        QCOMPARE(a.frames, frame_t(8000));
        QVERIFY(allNearly(a, 0, 3000, 0.25f));
        QVERIFY(allNearly(a, 3000, 6000, 0.f));
        QVERIFY(allNearly(a, 6000 + kFade, 8000 - kFade, 0.5f));
    }

    void splice_before_zero_is_clipped() {
        const frame_t n = 4000;
        QString recording = writeWav(ramp(n));
        QString out = newPath();
        Coverage::Range placed;
        QCOMPARE(TakeAudio::splice("", recording, 0, -1000, -1, out,
                                   &placed, kFade), QString());
        QCOMPARE(placed, Coverage::Range(0, 3000));
        Audio a = read(out);
        QCOMPARE(a.frames, frame_t(3000));
        QVERIFY(nearly(a.at(1500), rampValue(2500, n)));
    }

    void splice_with_no_fade() {
        QString old = writeWav(constant(3000, 0.25f));
        QString recording = writeWav(constant(1000, 0.5f));
        QString out = newPath();
        QCOMPARE(TakeAudio::splice(old, recording, 0, 1000, -1, out,
                                   nullptr, 0), QString());
        Audio a = read(out);
        QVERIFY(allNearly(a, 0, 1000, 0.25f));
        QVERIFY(allNearly(a, 1000, 2000, 0.5f));
        QVERIFY(allNearly(a, 2000, 3000, 0.25f));
    }

    // Shorter than two fades: they are shortened, and do not run into
    // each other or outside the recording
    void splice_shorter_than_the_fades() {
        QString old = writeWav(constant(3000, 0.25f));
        QString recording = writeWav(constant(50, 0.5f));
        QString out = newPath();
        QCOMPARE(TakeAudio::splice(old, recording, 0, 1000, -1, out,
                                   nullptr, kFade), QString());
        Audio a = read(out);
        QVERIFY(allNearly(a, 0, 1000, 0.25f));
        QVERIFY(allNearly(a, 1050, 3000, 0.25f));
        QVERIFY(monotonicBetween(a, 1000, 1025, 0.25f, 0.5f));
        QVERIFY(monotonicBetween(a, 1025, 1050, 0.5f, 0.25f));
        QVERIFY(a.at(1025) > 0.45f);
    }

    // The take keeps its channels whatever the device gives later
    void splice_mono_into_stereo() {
        Signal stereo;
        for (int i = 0; i < 3000; ++i) {
            stereo.push_back(0.2f);
            stereo.push_back(-0.2f);
        }
        QString old = writeWav(stereo, 2);
        QString recording = writeWav(constant(1000, 0.5f));
        QString out = newPath();
        QCOMPARE(TakeAudio::splice(old, recording, 0, 1000, -1, out,
                                   nullptr, 0), QString());
        Audio a = read(out);
        QCOMPARE(a.channels, 2);
        QCOMPARE(a.frames, frame_t(3000));
        QVERIFY(allNearly(a, 0, 1000, 0.2f, 0));
        QVERIFY(allNearly(a, 0, 1000, -0.2f, 1));
        QVERIFY(allNearly(a, 1000, 2000, 0.5f, 0));
        QVERIFY(allNearly(a, 1000, 2000, 0.5f, 1));
        QVERIFY(allNearly(a, 2000, 3000, -0.2f, 1));
    }

    void splice_stereo_into_mono() {
        Signal stereo;
        for (int i = 0; i < 1000; ++i) {
            stereo.push_back(0.6f);
            stereo.push_back(0.2f);
        }
        QString old = writeWav(constant(3000, 0.1f));
        QString recording = writeWav(stereo, 2);
        QString out = newPath();
        QCOMPARE(TakeAudio::splice(old, recording, 0, 1000, -1, out,
                                   nullptr, 0), QString());
        Audio a = read(out);
        QCOMPARE(a.channels, 1);
        QVERIFY(allNearly(a, 1000, 2000, 0.4f));
        QVERIFY(allNearly(a, 2000, 3000, 0.1f));
    }

    // Longer than the block the work is done in, with the recording
    // across a block boundary
    void splice_long_files() {
        const frame_t oldLength = 100000, n = 40000, position = 30000;
        QString old = writeWav(constant(oldLength, 0.25f));
        QString recording = writeWav(ramp(n));
        QString out = newPath();
        QCOMPARE(TakeAudio::splice(old, recording, 0, position, -1, out,
                                   nullptr, kFade), QString());
        Audio a = read(out);
        QCOMPARE(a.frames, oldLength);
        QVERIFY(allNearly(a, 0, position, 0.25f));
        for (frame_t k = kFade; k < n - kFade; k += 97) {
            QVERIFY2(nearly(a.at(position + k), rampValue(k, n)),
                     qPrintable(QString::number(k)));
        }
        QVERIFY(allNearly(a, position + n, oldLength, 0.25f));
    }

    void splice_errors() {
        QString old = writeWav(constant(3000, 0.25f));
        QString recording = writeWav(constant(1000, 0.5f));
        QString other = writeWav(constant(1000, 0.5f), 1, 48000.0);

        QString out = newPath();
        QVERIFY(TakeAudio::splice(old, m_dir.filePath("absent.wav"),
                                  0, 0, -1, out) != "");
        QVERIFY(!QFile::exists(out));

        // The take's own file gone missing: the message says so, and does
        // not claim that the file being written exists already
        QString missing = m_dir.filePath("absent.wav");
        QString error = TakeAudio::splice(missing, recording, 0, 0, -1, out);
        QVERIFY2(error.contains("absent.wav") && !error.contains(out),
                 qPrintable(error));
        QVERIFY(!QFile::exists(out));

        QVERIFY(TakeAudio::splice(old, other, 0, 0, -1, out) != "");
        QVERIFY(!QFile::exists(out));

        // nothing left of it after the latency, or before frame 0
        QVERIFY(TakeAudio::splice(old, recording, 1000, 0, -1, out) != "");
        QVERIFY(TakeAudio::splice(old, recording, 0, -1000, -1, out) != "");
        QVERIFY(TakeAudio::splice(old, recording, 0, 0, 0, out) != "");
        QVERIFY(!QFile::exists(out));

        // never over a file that is there, the take's own least of all
        QVERIFY(TakeAudio::splice(old, recording, 0, 0, -1, old) != "");
        QVERIFY(TakeAudio::splice(old, recording, 0, 0, -1, recording) != "");
        QVERIFY(TakeAudio::splice(old, recording, 0, 0, -1, "") != "");
        QVERIFY(allNearly(read(old), 0, 3000, 0.25f));
        QVERIFY(allNearly(read(recording), 0, 1000, 0.5f));
    }

    void erase_the_middle() {
        QString old = writeWav(constant(10000, 0.5f));
        QString out = newPath();
        QCOMPARE(TakeAudio::erase(old, { Coverage::Range(3000, 5000) }, out,
                                  kFade), QString());
        Audio a = read(out);
        QVERIFY(a.ok);
        QCOMPARE(a.frames, frame_t(10000));
        QVERIFY(allNearly(a, 0, 3000, 0.5f));
        QVERIFY(monotonicBetween(a, 3000, 3000 + kFade, 0.5f, 0.f));
        QVERIFY(allNearly(a, 3000 + kFade, 5000 - kFade, 0.f));
        QVERIFY(monotonicBetween(a, 5000 - kFade, 5000, 0.f, 0.5f));
        QVERIFY(allNearly(a, 5000, 10000, 0.5f));

        QVERIFY(allNearly(read(old), 0, 10000, 0.5f));
    }

    // Out of order, overlapping, and off both ends of the file
    void erase_several() {
        QString old = writeWav(constant(10000, 0.5f));
        QString out = newPath();
        Coverage::Ranges ranges {
            Coverage::Range(8000, 20000),
            Coverage::Range(2000, 3000),
            Coverage::Range(2500, 4000),
            Coverage::Range(-500, 1000),
            Coverage::Range(6000, 6000),
        };
        QCOMPARE(TakeAudio::erase(old, ranges, out, 0), QString());
        Audio a = read(out);
        QCOMPARE(a.frames, frame_t(10000));
        QVERIFY(allNearly(a, 0, 1000, 0.f));
        QVERIFY(allNearly(a, 1000, 2000, 0.5f));
        QVERIFY(allNearly(a, 2000, 4000, 0.f));
        QVERIFY(allNearly(a, 4000, 8000, 0.5f));
        QVERIFY(allNearly(a, 8000, 10000, 0.f));
    }

    void erase_nothing_is_a_copy() {
        QString old = writeWav(ramp(5000));
        QString out = newPath();
        QCOMPARE(TakeAudio::erase(old, {}, out), QString());
        Audio a = read(out);
        QCOMPARE(a.frames, frame_t(5000));
        for (frame_t i = 0; i < 5000; i += 50) {
            QVERIFY(nearly(a.at(i), rampValue(i, 5000)));
        }
    }

    void erase_errors() {
        QString old = writeWav(constant(3000, 0.5f));
        QString out = newPath();
        Coverage::Ranges ranges { Coverage::Range(1000, 2000) };
        QVERIFY(TakeAudio::erase(m_dir.filePath("absent.wav"), ranges, out) != "");
        QVERIFY(TakeAudio::erase("", ranges, out) != "");
        QVERIFY(!QFile::exists(out));
        QVERIFY(TakeAudio::erase(old, ranges, old) != "");
        QVERIFY(allNearly(read(old), 0, 3000, 0.5f));
    }

    // Converting a recording from a device that does not run at the
    // reference's rate. As long as it was in seconds, to the frame, in
    // as many channels, kept apart
    void resample_keeps_the_length_and_the_channels() {
        const frame_t n = 12345;
        Signal stereo;
        for (frame_t i = 0; i < n; ++i) {
            stereo.push_back(0.25f);
            stereo.push_back(-0.25f);
        }
        QString in = writeWav(stereo, 2, 48000.0);
        QString out = newPath();
        QCOMPARE(TakeAudio::resample(in, 44100.0, out), QString());
        QCOMPARE(TakeAudio::sampleRate(out), 44100.0);
        Audio a = read(out);
        QVERIFY(a.ok);
        QCOMPARE(a.channels, 2);
        QCOMPARE(a.frames, frame_t(std::llround(n * 44100.0 / 48000.0)));
        QVERIFY(allNearly(a, 1000, a.frames - 1000, 0.25f, 0));
        QVERIFY(allNearly(a, 1000, a.frames - 1000, -0.25f, 1));

        // Upwards, and longer than the blocks the work is done in
        const frame_t m = 100000;
        in = writeWav(ramp(m), 1, 44100.0);
        out = newPath();
        QCOMPARE(TakeAudio::resample(in, 48000.0, out), QString());
        QCOMPARE(TakeAudio::sampleRate(out), 48000.0);
        a = read(out);
        QCOMPARE(a.frames, frame_t(std::llround(m * 48000.0 / 44100.0)));

        // The original is as it was
        QCOMPARE(TakeAudio::sampleRate(in), 44100.0);
        QCOMPARE(read(in).frames, m);
    }

    // A click is where it was in time, to within a frame: near the
    // start, across the boundary of two of the blocks the work is done
    // in, and near the end, which the resampler holds back until
    // something follows it
    void resample_keeps_clicks_where_they_were() {
        const frame_t n = 50000;
        const std::vector<frame_t> clicks { 1000, 16390, 33000, n - 200 };
        for (double from : { 48000.0, 44100.0 }) {
            double to = (from == 48000.0 ? 44100.0 : 48000.0);
            Signal s(n, 0.f);
            for (frame_t c : clicks) s[c] = 0.9f;
            QString in = writeWav(s, 1, from);
            QString out = newPath();
            QCOMPARE(TakeAudio::resample(in, to, out), QString());
            Audio a = read(out);
            QVERIFY(a.ok);
            for (frame_t c : clicks) {
                double want = double(c) * to / from;
                frame_t peak = -1;
                float loudest = 0.f;
                for (frame_t i = frame_t(want) - 50; i <= frame_t(want) + 50;
                     ++i) {
                    if (i < 0 || i >= a.frames) continue;
                    if (std::fabs(a.at(i)) > loudest) {
                        loudest = std::fabs(a.at(i));
                        peak = i;
                    }
                }
                QVERIFY2(loudest > 0.3f && std::fabs(double(peak) - want) <= 1.0,
                         qPrintable(QString("a click at frame %1 at %2 Hz "
                                            "belongs at %3 at %4 Hz; the "
                                            "loudest frame near it is %5, "
                                            "at %6")
                                    .arg(c).arg(from).arg(want).arg(to)
                                    .arg(peak).arg(loudest)));
            }
        }
    }

    // A tone keeps its pitch, its phase and its level: frame for frame,
    // the same sine at the new rate
    void resample_keeps_a_tone() {
        const double hz = 1000.0, from = 48000.0, to = 44100.0;
        QString in = writeWav(TestSignals::sine(hz, from, int(from)), 1, from);
        QString out = newPath();
        QCOMPARE(TakeAudio::resample(in, to, out), QString());
        Audio a = read(out);
        QCOMPARE(a.frames, frame_t(to));
        // Away from the ends, where the tone starts and stops abruptly
        for (frame_t i = 1000; i < a.frames - 1000; ++i) {
            float want = float(0.5 * std::sin(2.0 * TestSignals::kPi * hz *
                                              double(i) / to));
            QVERIFY2(std::fabs(a.at(i) - want) < 0.005f,
                     qPrintable(QString("frame %1 is %2; a %3 Hz sine at "
                                        "%4 Hz is %5 there")
                                .arg(i).arg(a.at(i)).arg(hz).arg(to)
                                .arg(want)));
        }
    }

    void resample_errors() {
        QString in = writeWav(constant(1000, 0.5f), 1, 48000.0);
        QString other = writeWav(constant(1000, 0.25f));

        QString out = newPath();
        QVERIFY(TakeAudio::resample(m_dir.filePath("absent.wav"), 44100.0,
                                    out) != "");
        QVERIFY(TakeAudio::resample(in, 0.0, out) != "");
        QVERIFY(TakeAudio::resample(in, 44100.0, "") != "");
        QVERIFY(!QFile::exists(out));

        // never over a file that is there, the one read from least of all
        QVERIFY(TakeAudio::resample(in, 44100.0, in) != "");
        QVERIFY(TakeAudio::resample(in, 44100.0, other) != "");
        QVERIFY(allNearly(read(in), 0, 1000, 0.5f));
        QVERIFY(allNearly(read(other), 0, 1000, 0.25f));

        QCOMPARE(TakeAudio::sampleRate(in), 48000.0);
        QCOMPARE(TakeAudio::sampleRate(m_dir.filePath("absent.wav")), 0.0);
    }
};

#endif
