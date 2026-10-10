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

#ifndef TEST_TAKE_REPLACEMENT_H
#define TEST_TAKE_REPLACEMENT_H

// Tier 2: Replace Take Audio from Recording putting what its search
// found into the take, through files written here and read back. The
// recording is a ramp, so that every sample of the take says where in
// the recording it came from.

#include "../SingingTakes.h"
#include "../TakeReplacement.h"

#include "data/fileio/FileSource.h"
#include "data/fileio/WavFileReader.h"
#include "data/fileio/WavFileWriter.h"

#include <QDir>
#include <QObject>
#include <QtTest>
#include <QTemporaryDir>

#include <cmath>
#include <vector>

class TestTakeReplacement : public QObject
{
    Q_OBJECT

    typedef sv::sv_frame_t frame_t;
    static constexpr double kRate = 44100.0;

    QTemporaryDir m_dir;
    int m_counter = 0;

    QString write(const std::vector<float> &samples, double rate = kRate) {
        const QString path = m_dir.filePath
            (QString("audio-%1.wav").arg(++m_counter));
        sv::WavFileWriter writer(path, rate, 1,
                                 sv::WavFileWriter::WriteToTarget);
        sv::floatvec_t data(samples.begin(), samples.end());
        if (!writer.isOK() || !writer.putInterleavedFrames(data) ||
            !writer.close()) {
            return "";
        }
        return path;
    }

    static std::vector<float> read(QString path) {
        sv::WavFileReader reader { sv::FileSource(path) };
        if (!reader.isOK()) return {};
        const auto data = reader.getInterleavedFrames
            (0, reader.getFrameCount());
        return std::vector<float>(data.begin(), data.end());
    }

    QString directory() {
        QDir dir(m_dir.path());
        dir.mkpath("takes");
        return dir.filePath("takes");
    }

    // The take: silence, then 0.25 recorded from 1000 to 9000
    void makeTake(SingingTakes &takes) {
        const QString recorded = write(std::vector<float>(8000, 0.25f));
        QVERIFY(takes.spliceRecording(recorded, 0, 1000, -1, directory())
                .isEmpty());
    }

    // The recording: a ramp, frame f at f / 100000
    static std::vector<float> ramp(frame_t frames) {
        std::vector<float> v(static_cast<size_t>(frames));
        for (size_t i = 0; i < v.size(); ++i) v[i] = float(i) / 100000.f;
        return v;
    }

    static RecordingAlignment::Segment found(frame_t start, frame_t end,
                                             double offset, double gain) {
        RecordingAlignment::Segment s;
        s.start = start;
        s.end = end;
        s.match.found = true;
        s.match.offset = offset;
        s.match.gain = gain;
        s.match.confidence = 0.99;
        return s;
    }

    static RecordingAlignment::Segment left(frame_t start, frame_t end,
                                            RecordingAlignment::Match::Left why) {
        RecordingAlignment::Segment s;
        s.start = start;
        s.end = end;
        s.match.left = why;
        return s;
    }

    QStringList takeFiles() {
        return QDir(directory()).entryList({ "*.wav" }, QDir::Files);
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void init() {
        QDir(directory()).removeRecursively();
    }

    // Two stretches found, one left: each found one is the recording's
    // from its offset, at its gain, but for the splice's fades; what lies
    // between and the one left are as they were. Of the take files the
    // replacement wrote, only the last is left: the one between went at
    // once
    void the_found_stretches_go_in() {
        SingingTakes takes;
        makeTake(takes);
        if (QTest::currentTestFailed()) return;
        const QString before = takes.getAudioPath();
        const Coverage coverage = takes.getCoverage();
        const QString recording = write(ramp(30000));
        using Left = RecordingAlignment::Match::Left;
        const std::vector<RecordingAlignment::Segment> segments = {
            found(1000, 4000, 10000.0, 2.0),
            left(4000, 4300, Left::ShortSession),
            found(4300, 9000, 20000.0, 1.0) };

        TakeReplacement::Result result;
        const QString error = TakeReplacement::replace
            (takes, segments, recording, kRate, kRate, 30000, directory(),
             result);
        QCOMPARE(error, QString());
        QCOMPARE(result.replaced, 2);
        QCOMPARE(result.whole, Coverage::Range(1000, 9000));
        QCOMPARE(takes.getCoverage(), coverage);
        QVERIFY(takes.getAudioPath() != before);

        const std::vector<float> take = read(takes.getAudioPath());
        QCOMPARE(int(take.size()), 9000);
        // Clear of the 5 ms fades at each end of a stretch
        const frame_t fade = frame_t(0.005 * kRate) + 2;
        for (frame_t f = 1000 + fade; f < 4000 - fade; ++f) {
            QVERIFY2(std::fabs(take[size_t(f)] -
                               2.0f * float(f + 10000) / 100000.f) < 1e-5f,
                     qPrintable(QString("frame %1: %2").arg(f)
                                .arg(take[size_t(f)])));
        }
        for (frame_t f = 4300 + fade; f < 9000 - fade; ++f) {
            QVERIFY2(std::fabs(take[size_t(f)] -
                               float(f + 20000) / 100000.f) < 1e-5f,
                     qPrintable(QString("frame %1").arg(f)));
        }
        QCOMPARE(take[4150], 0.25f);
        QCOMPARE(take[500], 0.f);

        // The take before (for undo) and the take after, nothing between
        QCOMPARE(takeFiles().size(), 2);
        QVERIFY(takes.getWrittenPaths().contains(before));
        QCOMPARE(takes.getWrittenPaths().size(), 2);
        QCOMPARE(int(result.lines.size()), 3);
        QVERIFY2(result.lines[1].contains("too short to look for, left as "
                                          "it was"),
                 qPrintable(result.lines[1]));
        QVERIFY2(result.lines[0].contains("brought up by 6.0 dB"),
                 qPrintable(result.lines[0]));
    }

    // A stretch that reaches past the recording's end is cut at it: what
    // lies past it stays as the take had it
    void a_stretch_is_kept_within_the_recording() {
        SingingTakes takes;
        makeTake(takes);
        if (QTest::currentTestFailed()) return;
        const QString recording = write(ramp(15000));
        TakeReplacement::Result result;
        QCOMPARE(TakeReplacement::replace
                 (takes, { found(1000, 9000, 10000.0, 1.0) }, recording,
                  kRate, kRate, 15000, directory(), result), QString());
        const std::vector<float> take = read(takes.getAudioPath());
        // The recording's last frame, 14999, is the take's 4999: the
        // frame after the stretch's end has to be on the recording
        QCOMPARE(result.whole.end, frame_t(4999));
        QCOMPARE(take[6000], 0.25f);
        QVERIFY(std::fabs(take[3000] - 13000.f / 100000.f) < 1e-5f);
    }

    // A recording that cannot be read: the take as it was, and no file
    // of the take's but the one it had
    void a_failure_leaves_the_take_as_it_was() {
        SingingTakes takes;
        makeTake(takes);
        if (QTest::currentTestFailed()) return;
        const QString before = takes.getAudioPath();
        TakeReplacement::Result result;
        const QString error = TakeReplacement::replace
            (takes, { found(1000, 4000, 0.0, 1.0) },
             m_dir.filePath("nothing.wav"), kRate, kRate, 30000,
             directory(), result);
        QVERIFY(error != "");
        QCOMPARE(takes.getAudioPath(), before);
        QCOMPARE(takeFiles().size(), 1);
    }

    // Nothing found: nothing to do, and said
    void nothing_found_is_nothing_replaced() {
        SingingTakes takes;
        makeTake(takes);
        if (QTest::currentTestFailed()) return;
        const QString before = takes.getAudioPath();
        using Left = RecordingAlignment::Match::Left;
        TakeReplacement::Result result;
        const QString error = TakeReplacement::replace
            (takes, { left(1000, 1200, Left::TooShort),
                      left(1200, 9000, Left::OutsideRecording) },
             write(ramp(30000)), kRate, kRate, 30000, directory(), result);
        QVERIFY(error.contains("Nothing in the take could be replaced"));
        QCOMPARE(takes.getAudioPath(), before);
        QVERIFY2(result.lines.size() == 2 &&
                 result.lines[1].contains("not in the recording"),
                 qPrintable(result.lines.join(" | ")));
    }
};

#endif
