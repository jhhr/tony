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

#ifndef TEST_SINGING_TAKES_H
#define TEST_SINGING_TAKES_H

// Tier 2: the state of a singing take — which audio file it is in, what
// of it holds recorded material, and what recording into it does to
// both. No window and no audio device: the files are written here and
// read back.

#include "../SingingTakes.h"

#include "data/fileio/FileSource.h"
#include "data/fileio/WavFileReader.h"
#include "data/fileio/WavFileWriter.h"

#include <QObject>
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include <cmath>
#include <vector>

class TestSingingTakes : public QObject
{
    Q_OBJECT

    typedef sv::sv_frame_t frame_t;

    static constexpr double kRate = 44100.0;

    QTemporaryDir m_dir;
    int m_fileCounter = 0;

    // A recording of the given length, at a level that says which one it is
    QString writeRecording(frame_t frames, float level) {
        QString path = m_dir.filePath
            (QString("recorded-%1.wav").arg(++m_fileCounter));
        sv::WavFileWriter writer(path, kRate, 1,
                                 sv::WavFileWriter::WriteToTarget);
        sv::floatvec_t data(frames, level);
        if (!writer.isOK() || !writer.putInterleavedFrames(data) ||
            !writer.close()) {
            return "";
        }
        return path;
    }

    QString takeDirectory() {
        QDir dir(m_dir.path());
        dir.mkpath("takes");
        return dir.filePath("takes");
    }

    static frame_t framesIn(QString path) {
        sv::WavFileReader reader { sv::FileSource(path) };
        return reader.isOK() ? reader.getFrameCount() : -1;
    }

    static float sampleAt(QString path, frame_t frame) {
        sv::WavFileReader reader { sv::FileSource(path) };
        if (!reader.isOK()) return 0.f;
        auto data = reader.getInterleavedFrames(frame, 1);
        return data.empty() ? 0.f : data[0];
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void init() {
        SingingTakes::setOverwriteConfirmationWanted(true);
    }

    void nothing_to_start_with() {
        SingingTakes takes;
        QVERIFY(!takes.haveTake());
        QCOMPARE(takes.getAudioPath(), QString());
        QVERIFY(takes.getCoverage().isEmpty());
        QVERIFY(takes.getSupersededPaths().isEmpty());
        // Nothing is recorded, so nothing can be recorded over
        QVERIFY(!takes.coversPosition(0));
        QVERIFY(!takes.shouldConfirmRecordingAt(0));
    }

    void whole_file_take() {
        SingingTakes takes;
        takes.setWholeFileTake("/somewhere/sung.wav", 1000);
        QVERIFY(takes.haveTake());
        QCOMPARE(takes.getAudioPath(), QString("/somewhere/sung.wav"));
        QCOMPARE(int(takes.getCoverage().getRanges().size()), 1);
        QCOMPARE(takes.getCoverage().getRanges()[0], Coverage::Range(0, 1000));
        QVERIFY(takes.coversPosition(999));
        QVERIFY(!takes.coversPosition(1000));

        // Another file loaded in its place: the one before is kept, since
        // it may be wanted again by undo
        takes.setWholeFileTake("/somewhere/else.wav", 10);
        QCOMPARE(takes.getAudioPath(), QString("/somewhere/else.wav"));
        QCOMPARE(int(takes.getCoverage().getRanges().size()), 1);
        QCOMPARE(takes.getCoverage().getRanges()[0], Coverage::Range(0, 10));
        QCOMPARE(takes.getSupersededPaths(),
                 QStringList { "/somewhere/sung.wav" });

        takes.clear();
        QVERIFY(!takes.haveTake());
        QVERIFY(takes.getCoverage().isEmpty());
        QVERIFY(takes.getSupersededPaths().isEmpty());
    }

    // Every audio file of a take gets a name of its own, in the directory
    // asked for, and never one that is taken: TakeAudio refuses to write
    // over a file, so a name that collides would lose a recording
    void every_file_has_its_own_name() {
        QString dir = takeDirectory();
        QStringList made;

        for (int i = 0; i < 4; ++i) {
            QString path = SingingTakes::nextAudioPath(dir);
            QVERIFY(!path.isEmpty());
            QCOMPARE(QFileInfo(path).absolutePath(),
                     QFileInfo(dir).absoluteFilePath());
            QVERIFY2(!QFileInfo::exists(path), qPrintable(path));
            QVERIFY2(!made.contains(path), qPrintable(path));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.close();
            made << path;
        }
    }

    // The first recording of a take, made from part of the way in
    void splice_first_recording() {
        SingingTakes takes;
        QString recording = writeRecording(4000, 0.5f);
        QVERIFY(!recording.isEmpty());

        Coverage::Range placed;
        QString error = takes.spliceRecording(recording, 1000, 2000, -1,
                                              takeDirectory(), &placed);
        QCOMPARE(error, QString());

        // The first 1000 frames of the recording are the latency: what was
        // recorded before the singer could have heard frame 2000
        QCOMPARE(placed, Coverage::Range(2000, 5000));
        QVERIFY(takes.haveTake());
        QCOMPARE(int(takes.getCoverage().getRanges().size()), 1);
        QCOMPARE(takes.getCoverage().getRanges()[0], Coverage::Range(2000, 5000));
        QVERIFY(takes.getSupersededPaths().isEmpty());

        QString path = takes.getAudioPath();
        QCOMPARE(framesIn(path), frame_t(5000));
        QCOMPARE(sampleAt(path, 500), 0.f);
        QVERIFY(std::fabs(sampleAt(path, 3500) - 0.5f) < 1e-3f);
    }

    // A second recording, into the silence after the first: the file grows,
    // the gap stays as it was and the coverage has two ranges
    void splice_into_a_gap() {
        SingingTakes takes;
        QString first = writeRecording(1000, 0.5f);
        QString second = writeRecording(1000, 0.25f);
        QVERIFY(takes.spliceRecording(first, 0, 0, -1,
                                      takeDirectory()).isEmpty());
        QString firstPath = takes.getAudioPath();

        Coverage::Range placed;
        QVERIFY(takes.spliceRecording(second, 0, 5000, -1, takeDirectory(),
                                      &placed).isEmpty());
        QCOMPARE(placed, Coverage::Range(5000, 6000));

        QCOMPARE(int(takes.getCoverage().getRanges().size()), 2);
        QCOMPARE(takes.getCoverage().getRanges()[0], Coverage::Range(0, 1000));
        QCOMPARE(takes.getCoverage().getRanges()[1], Coverage::Range(5000, 6000));

        QVERIFY(takes.getAudioPath() != firstPath);
        QCOMPARE(takes.getSupersededPaths(), QStringList { firstPath });
        QVERIFY2(QFileInfo::exists(firstPath),
                 "the file the take had before was not kept");

        QString path = takes.getAudioPath();
        QCOMPARE(framesIn(path), frame_t(6000));
        QVERIFY(std::fabs(sampleAt(path, 500) - 0.5f) < 1e-3f);
        QCOMPARE(sampleAt(path, 3000), 0.f);
        QVERIFY(std::fabs(sampleAt(path, 5500) - 0.25f) < 1e-3f);
    }

    // Recording over material that is there: the ranges join, and the new
    // material is what is heard where it landed
    void splice_over_existing() {
        SingingTakes takes;
        QString first = writeRecording(4000, 0.5f);
        QString second = writeRecording(1000, 0.25f);
        QVERIFY(takes.spliceRecording(first, 0, 0, -1,
                                      takeDirectory()).isEmpty());
        QVERIFY(takes.spliceRecording(second, 0, 2000, -1,
                                      takeDirectory()).isEmpty());

        QCOMPARE(int(takes.getCoverage().getRanges().size()), 1);
        QCOMPARE(takes.getCoverage().getRanges()[0], Coverage::Range(0, 4000));

        QString path = takes.getAudioPath();
        QCOMPARE(framesIn(path), frame_t(4000));
        QVERIFY(std::fabs(sampleAt(path, 1000) - 0.5f) < 1e-3f);  // kept
        QVERIFY(std::fabs(sampleAt(path, 2500) - 0.25f) < 1e-3f); // replaced
        QVERIFY(std::fabs(sampleAt(path, 3500) - 0.5f) < 1e-3f);  // kept
    }

    void splice_failure_leaves_the_take_alone() {
        SingingTakes takes;
        QString recording = writeRecording(1000, 0.5f);
        QVERIFY(takes.spliceRecording(recording, 0, 0, -1,
                                      takeDirectory()).isEmpty());
        QString path = takes.getAudioPath();
        Coverage before = takes.getCoverage();

        // Nothing of the recording is left once the latency is taken off
        QString error = takes.spliceRecording(recording, 2000, 0, -1,
                                              takeDirectory());
        QVERIFY(!error.isEmpty());
        QCOMPARE(takes.getAudioPath(), path);
        QVERIFY(takes.getCoverage() == before);
        QVERIFY(takes.getSupersededPaths().isEmpty());

        // and a recording that is not there at all
        error = takes.spliceRecording(m_dir.filePath("not-a-file.wav"), 0, 0,
                                      -1, takeDirectory());
        QVERIFY(!error.isEmpty());
        QCOMPARE(takes.getAudioPath(), path);
        QVERIFY(takes.getCoverage() == before);
    }

    // The question before recording over something: asked inside the
    // covered ranges only, and not at all once the user has said so
    void overwrite_question() {
        SingingTakes takes;
        QString recording = writeRecording(1000, 0.5f);
        QVERIFY(takes.spliceRecording(recording, 0, 1000, -1,
                                      takeDirectory()).isEmpty());
        // coverage is [1000, 2000)

        QVERIFY(SingingTakes::isOverwriteConfirmationWanted());
        QVERIFY(takes.shouldConfirmRecordingAt(1000));
        QVERIFY(takes.shouldConfirmRecordingAt(1999));

        // In a gap, before or after: recording there takes nothing away,
        // even if it runs on into what is there
        QVERIFY(!takes.shouldConfirmRecordingAt(0));
        QVERIFY(!takes.shouldConfirmRecordingAt(999));
        QVERIFY(!takes.shouldConfirmRecordingAt(2000));

        SingingTakes::setOverwriteConfirmationWanted(false);
        QVERIFY(!SingingTakes::isOverwriteConfirmationWanted());
        QVERIFY(takes.coversPosition(1500));
        QVERIFY(!takes.shouldConfirmRecordingAt(1500));

        SingingTakes::setOverwriteConfirmationWanted(true);
        QVERIFY(takes.shouldConfirmRecordingAt(1500));
    }
};

#endif
