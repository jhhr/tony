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

    // Several takes (spec 5.3): a list, with one of them the active one,
    // which is the take every single-take call is about

    void takes_are_a_list() {
        SingingTakes takes;
        QCOMPARE(takes.getTakeCount(), 0);
        QCOMPARE(takes.getActiveIndex(), -1);
        QCOMPARE(takes.getActiveName(), QString());
        QVERIFY(takes.getTakeNames().isEmpty());
        QVERIFY(!takes.getTake(0));

        // An empty take: there, active, and with nothing in it
        QCOMPARE(takes.addTake(), QString("Take 1"));
        QCOMPARE(takes.getTakeCount(), 1);
        QCOMPARE(takes.getActiveIndex(), 0);
        QVERIFY(!takes.haveTake());
        QVERIFY(takes.getCoverage().isEmpty());

        takes.setWholeFileTake("/somewhere/one.wav", 1000);
        QVERIFY(takes.haveTake());

        // A second take of its own: the first is left as it is
        QCOMPARE(takes.addTake(), QString("Take 2"));
        QCOMPARE(takes.getActiveIndex(), 1);
        QVERIFY(!takes.haveTake());
        QVERIFY(takes.getCoverage().isEmpty());
        QCOMPARE(takes.getTakeNames(), QStringList({ "Take 1", "Take 2" }));

        takes.setWholeFileTake("/somewhere/two.wav", 2000);

        // Switching is the active index, and nothing else
        QVERIFY(takes.setActiveIndex(0));
        QCOMPARE(takes.getAudioPath(), QString("/somewhere/one.wav"));
        QCOMPARE(takes.getCoverage().getRanges()[0], Coverage::Range(0, 1000));
        QVERIFY(takes.setActiveIndex(1));
        QCOMPARE(takes.getAudioPath(), QString("/somewhere/two.wav"));
        QCOMPARE(takes.getCoverage().getRanges()[0], Coverage::Range(0, 2000));

        QVERIFY(!takes.setActiveIndex(2));
        QVERIFY(!takes.setActiveIndex(-1));
        QCOMPARE(takes.getActiveIndex(), 1);

        QCOMPARE(takes.indexOf("Take 1"), 0);
        QCOMPARE(takes.indexOf("Take 3"), -1);

        takes.clear();
        QCOMPARE(takes.getTakeCount(), 0);
        QCOMPARE(takes.getActiveIndex(), -1);
    }

    // The first recording of a session records into a take of its own,
    // without anyone having to ask for one
    void first_recording_makes_a_take() {
        SingingTakes takes;
        QString recording = writeRecording(1000, 0.5f);
        QVERIFY(takes.spliceRecording(recording, 0, 0, -1,
                                      takeDirectory()).isEmpty());
        QCOMPARE(takes.getTakeCount(), 1);
        QCOMPARE(takes.getActiveName(), QString("Take 1"));
        QVERIFY(takes.haveTake());
    }

    // A name is never used twice in a session, however many takes have
    // been deleted since: the layers of a take that has gone may still be
    // in the document, under the name it had
    void take_names_are_not_reused() {
        SingingTakes takes;
        QCOMPARE(takes.addTake(), QString("Take 1"));
        QCOMPARE(takes.addTake(), QString("Take 2"));
        QVERIFY(takes.removeTake(1));
        QCOMPARE(takes.addTake(), QString("Take 3"));

        // A name of the caller's own, and one that is taken already
        QCOMPARE(takes.addTake("Chorus"), QString("Chorus"));
        QCOMPARE(takes.addTake("Chorus"), QString("Take 4"));

        takes.clear();
        QCOMPARE(takes.addTake(), QString("Take 1"));
    }

    void rename_a_take() {
        SingingTakes takes;
        takes.addTake();
        takes.addTake();

        QVERIFY(takes.renameTake(0, "Chorus"));
        QCOMPARE(takes.getTakeNames(), QStringList({ "Chorus", "Take 2" }));
        QCOMPARE(takes.indexOf("Chorus"), 0);

        // The name it has, which is not a change at all
        QVERIFY(takes.renameTake(0, "Chorus"));

        // Empty, whitespace only, another take's, and no such take
        QVERIFY(!takes.renameTake(0, ""));
        QVERIFY(!takes.renameTake(0, "  "));
        QVERIFY(!takes.renameTake(0, "Take 2"));
        QVERIFY(!takes.renameTake(2, "Verse"));
        QCOMPARE(takes.getTakeNames(), QStringList({ "Chorus", "Take 2" }));

        QVERIFY(takes.renameTake(1, "  Verse  "));
        QCOMPARE(takes.getTakeNames(), QStringList({ "Chorus", "Verse" }));
    }

    // Deleting the active take leaves a neighbour active, and deleting the
    // last of them leaves no take at all
    void remove_take_activates_a_neighbour() {
        SingingTakes takes;
        takes.addTake();
        takes.addTake();
        takes.addTake();
        QCOMPARE(takes.getActiveIndex(), 2);

        QVERIFY(takes.removeTake(2));
        QCOMPARE(takes.getActiveIndex(), 1);

        // One before the active take: the same take stays active
        QVERIFY(takes.setActiveIndex(1));
        QVERIFY(takes.removeTake(0));
        QCOMPARE(takes.getActiveIndex(), 0);
        QCOMPARE(takes.getTakeNames(), QStringList { "Take 2" });

        QVERIFY(takes.removeTake(0));
        QCOMPARE(takes.getTakeCount(), 0);
        QCOMPARE(takes.getActiveIndex(), -1);
        QVERIFY(!takes.haveTake());

        QVERIFY(!takes.removeTake(0));
    }

    // A duplicate shares the audio file of the take it was made from
    // until one of them is recorded into or erased from, which writes a
    // new file anyway (spec 5.4).  So a file is in use while any take
    // refers to it
    void duplicate_shares_the_audio_file() {
        SingingTakes takes;
        QString recording = writeRecording(1000, 0.5f);
        QVERIFY(takes.spliceRecording(recording, 0, 0, -1,
                                      takeDirectory()).isEmpty());
        QString shared = takes.getAudioPath();
        Coverage coverage = takes.getCoverage();

        QCOMPARE(takes.duplicateActiveTake(), QString("Take 2"));
        QCOMPARE(takes.getTakeCount(), 2);
        QCOMPARE(takes.getActiveIndex(), 1);
        QCOMPARE(takes.getAudioPath(), shared);
        QVERIFY(takes.getCoverage() == coverage);

        // No file was written and none superseded
        QCOMPARE(takes.getWrittenPaths(), QStringList { shared });
        QVERIFY(takes.getSupersededPaths().isEmpty());
        QVERIFY(takes.unusedWrittenFiles().isEmpty());

        // Recording into the copy writes a file of its own; the take it
        // was copied from is untouched, and still uses the shared file
        QVERIFY(takes.spliceRecording(recording, 0, 5000, -1,
                                      takeDirectory()).isEmpty());
        QVERIFY(takes.getAudioPath() != shared);
        QCOMPARE(takes.getTake(0)->audioPath, shared);
        QVERIFY(takes.getTake(0)->coverage == coverage);

        // The shared file has been superseded for the copy, but the first
        // take still plays it, so it is not ours to delete
        QVERIFY(takes.getSupersededPaths().contains(shared));
        QVERIFY2(takes.unusedWrittenFiles().isEmpty(),
                 "a file another take is using was up for deletion");

        // ... and once that take has gone, it is
        QVERIFY(takes.removeTake(0));
        QCOMPARE(takes.unusedWrittenFiles(), QStringList { shared });
    }

    // A take with no audio yet: its name is there, and the calls about the
    // active take say there is nothing
    void a_take_with_no_audio() {
        SingingTakes takes;
        takes.addTake();
        QVERIFY(!takes.haveTake());
        QCOMPARE(takes.getAudioPath(), QString());
        QVERIFY(takes.getCoverage().isEmpty());
        QVERIFY(!takes.coversPosition(0));
        QVERIFY(!takes.shouldConfirmRecordingAt(0));
        QVERIFY(takes.duplicateActiveTake() != "");
        QVERIFY(!takes.haveTake());

        // Nothing to erase, and no file written trying
        Coverage::Ranges erased { Coverage::Range(1, 2) };
        QCOMPARE(takes.eraseRanges(Coverage::Ranges { Coverage::Range(0, 100) },
                                   takeDirectory(), &erased), QString());
        QVERIFY(erased.empty());
        QVERIFY(takes.getWrittenPaths().isEmpty());
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

    // Erasing from the middle of a recording: the file is as long as it
    // was, silent where the range was, and the coverage is split in two
    void erase_the_middle() {
        SingingTakes takes;
        QString recording = writeRecording(4000, 0.5f);
        QVERIFY(takes.spliceRecording(recording, 0, 0, -1,
                                      takeDirectory()).isEmpty());
        QString before = takes.getAudioPath();

        Coverage::Ranges erased;
        QCOMPARE(takes.eraseRanges(Coverage::Ranges { Coverage::Range(1000, 2000) },
                                   takeDirectory(), &erased), QString());

        QCOMPARE(int(erased.size()), 1);
        QCOMPARE(erased[0], Coverage::Range(1000, 2000));

        QCOMPARE(int(takes.getCoverage().getRanges().size()), 2);
        QCOMPARE(takes.getCoverage().getRanges()[0], Coverage::Range(0, 1000));
        QCOMPARE(takes.getCoverage().getRanges()[1], Coverage::Range(2000, 4000));

        QString path = takes.getAudioPath();
        QVERIFY(path != before);
        QCOMPARE(takes.getSupersededPaths(), QStringList { before });
        QVERIFY2(QFileInfo::exists(before),
                 "the file the take had before was not kept");

        // As long as it was, and silent only where the erase went.  The
        // samples looked at are clear of the 5 ms fades at the edges
        QCOMPARE(framesIn(path), frame_t(4000));
        QVERIFY(std::fabs(sampleAt(path, 500) - 0.5f) < 1e-3f);
        QCOMPARE(sampleAt(path, 1500), 0.f);
        QVERIFY(std::fabs(sampleAt(path, 3000) - 0.5f) < 1e-3f);
    }

    // Several ranges at once, out of order, and one of them trimming the
    // end of the recording
    void erase_several_ranges() {
        SingingTakes takes;
        QString recording = writeRecording(4000, 0.5f);
        QVERIFY(takes.spliceRecording(recording, 0, 0, -1,
                                      takeDirectory()).isEmpty());

        Coverage::Ranges erased;
        QCOMPARE(takes.eraseRanges(Coverage::Ranges { Coverage::Range(3000, 4000),
                                                      Coverage::Range(1000, 2000) },
                                   takeDirectory(), &erased), QString());

        QCOMPARE(int(erased.size()), 2);
        QCOMPARE(erased[0], Coverage::Range(1000, 2000));
        QCOMPARE(erased[1], Coverage::Range(3000, 4000));

        QCOMPARE(int(takes.getCoverage().getRanges().size()), 2);
        QCOMPARE(takes.getCoverage().getRanges()[0], Coverage::Range(0, 1000));
        QCOMPARE(takes.getCoverage().getRanges()[1], Coverage::Range(2000, 3000));

        QString path = takes.getAudioPath();
        QCOMPARE(framesIn(path), frame_t(4000));
        QCOMPARE(sampleAt(path, 1500), 0.f);
        QVERIFY(std::fabs(sampleAt(path, 2500) - 0.5f) < 1e-3f);
        QCOMPARE(sampleAt(path, 3500), 0.f);
    }

    // What is asked for is clipped to the coverage: silence that was
    // never recorded holds nothing to erase, and a selection running
    // past the singing must not make the file any longer
    void erase_is_clipped_to_the_coverage() {
        SingingTakes takes;
        QString recording = writeRecording(2000, 0.5f);
        // coverage is [2000, 4000)
        QVERIFY(takes.spliceRecording(recording, 0, 2000, -1,
                                      takeDirectory()).isEmpty());

        Coverage::Ranges erased;
        QCOMPARE(takes.eraseRanges(Coverage::Ranges { Coverage::Range(0, 3000) },
                                   takeDirectory(), &erased), QString());

        QCOMPARE(int(erased.size()), 1);
        QCOMPARE(erased[0], Coverage::Range(2000, 3000));
        QCOMPARE(int(takes.getCoverage().getRanges().size()), 1);
        QCOMPARE(takes.getCoverage().getRanges()[0], Coverage::Range(3000, 4000));
        QCOMPARE(framesIn(takes.getAudioPath()), frame_t(4000));
    }

    // A selection with no recorded singing in it: nothing is written and
    // nothing changes, and it is not an error
    void erase_where_nothing_was_recorded() {
        SingingTakes takes;
        QString recording = writeRecording(1000, 0.5f);
        QVERIFY(takes.spliceRecording(recording, 0, 1000, -1,
                                      takeDirectory()).isEmpty());
        QString path = takes.getAudioPath();
        Coverage before = takes.getCoverage();

        Coverage::Ranges erased { Coverage::Range(1, 2) };
        QCOMPARE(takes.eraseRanges(Coverage::Ranges { Coverage::Range(0, 1000) },
                                   takeDirectory(), &erased), QString());
        QVERIFY(erased.empty());
        QCOMPARE(takes.getAudioPath(), path);
        QVERIFY(takes.getCoverage() == before);
        QVERIFY(takes.getSupersededPaths().isEmpty());

        // and no ranges at all
        QCOMPARE(takes.eraseRanges(Coverage::Ranges {}, takeDirectory(),
                                   &erased), QString());
        QVERIFY(erased.empty());
        QCOMPARE(takes.getAudioPath(), path);
    }

    // Erasing all there is: the take stays, with an audio file that is
    // silent throughout and nothing covered
    void erase_the_whole_take() {
        SingingTakes takes;
        QString recording = writeRecording(2000, 0.5f);
        QVERIFY(takes.spliceRecording(recording, 0, 0, -1,
                                      takeDirectory()).isEmpty());

        QCOMPARE(takes.eraseRanges(Coverage::Ranges { Coverage::Range(0, 2000) },
                                   takeDirectory()), QString());

        QVERIFY(takes.haveTake());
        QVERIFY(takes.getCoverage().isEmpty());
        QCOMPARE(framesIn(takes.getAudioPath()), frame_t(2000));
        QCOMPARE(sampleAt(takes.getAudioPath(), 1000), 0.f);
    }

    void erase_failure_leaves_the_take_alone() {
        SingingTakes takes;
        takes.setWholeFileTake(m_dir.filePath("not-a-file.wav"), 1000);
        Coverage before = takes.getCoverage();

        Coverage::Ranges erased { Coverage::Range(1, 2) };
        QString error = takes.eraseRanges
            (Coverage::Ranges { Coverage::Range(0, 500) }, takeDirectory(),
             &erased);
        QVERIFY(!error.isEmpty());
        QVERIFY(erased.empty());
        QCOMPARE(takes.getAudioPath(), m_dir.filePath("not-a-file.wav"));
        QVERIFY(takes.getCoverage() == before);
        QVERIFY(takes.getSupersededPaths().isEmpty());
    }

    // Undo and redo swap the take between two files it has already had:
    // neither is superseded by the other again, however often they change
    // places, and neither becomes a file this run wrote
    void restore_take_supersedes_nothing() {
        SingingTakes takes;
        QString recording = writeRecording(1000, 0.5f);
        QVERIFY(takes.spliceRecording(recording, 0, 0, -1,
                                      takeDirectory()).isEmpty());
        QString first = takes.getAudioPath();
        QVERIFY(takes.spliceRecording(recording, 0, 5000, -1,
                                      takeDirectory()).isEmpty());
        QString second = takes.getAudioPath();
        Coverage after = takes.getCoverage();
        QCOMPARE(takes.getSupersededPaths(), QStringList { first });

        Coverage before;
        before.add(0, 1000);

        takes.restoreTake(first, before);
        QCOMPARE(takes.getAudioPath(), first);
        QVERIFY(takes.getCoverage() == before);
        QCOMPARE(takes.getSupersededPaths(), QStringList { first });
        QCOMPARE(takes.getWrittenPaths(), QStringList({ first, second }));

        takes.restoreTake(second, after);
        QCOMPARE(takes.getAudioPath(), second);
        QVERIFY(takes.getCoverage() == after);
        QCOMPARE(takes.getSupersededPaths(), QStringList { first });
        QCOMPARE(takes.getWrittenPaths(), QStringList({ first, second }));

        // Undo of the very first recording of a take: no take at all
        takes.restoreTake("", Coverage());
        QVERIFY(!takes.haveTake());
        QVERIFY(takes.getCoverage().isEmpty());
    }

    // What a session close may delete: the files this run wrote that
    // nothing refers to any more.  Never the file the take is in, and
    // never a file the user brought, however thoroughly superseded
    void only_unused_files_we_wrote_are_deleted() {
        SingingTakes takes;

        // The user's own singing track, recorded over twice
        QString loaded = writeRecording(1000, 0.5f);
        takes.setWholeFileTake(loaded, 1000);

        QString recording = writeRecording(1000, 0.25f);
        QVERIFY(takes.spliceRecording(recording, 0, 2000, -1,
                                      takeDirectory()).isEmpty());
        QString first = takes.getAudioPath();
        QVERIFY(takes.spliceRecording(recording, 0, 5000, -1,
                                      takeDirectory()).isEmpty());
        QString second = takes.getAudioPath();

        QCOMPARE(takes.getWrittenPaths(), QStringList({ first, second }));
        QVERIFY(takes.getSupersededPaths().contains(loaded));

        QCOMPARE(takes.unusedWrittenFiles(), QStringList { first });
        QCOMPARE(takes.removeUnusedFiles(), QStringList { first });

        QVERIFY2(!QFileInfo::exists(first), "a superseded file was kept");
        QVERIFY2(QFileInfo::exists(second), "the take's own file was deleted");
        QVERIFY2(QFileInfo::exists(loaded), "the user's own file was deleted");

        // and nothing is deleted twice
        QVERIFY(takes.unusedWrittenFiles().isEmpty());
        QVERIFY(takes.removeUnusedFiles().isEmpty());
    }

    // The file a saved session names must be there when that session is
    // opened again, however many recordings have superseded it since
    void a_saved_session_keeps_its_file() {
        SingingTakes takes;
        QString recording = writeRecording(1000, 0.5f);
        QVERIFY(takes.spliceRecording(recording, 0, 0, -1,
                                      takeDirectory()).isEmpty());
        QString saved = takes.getAudioPath();
        takes.protectPath(saved);

        QVERIFY(takes.spliceRecording(recording, 0, 5000, -1,
                                      takeDirectory()).isEmpty());
        QString second = takes.getAudioPath();
        QVERIFY(takes.unusedWrittenFiles().isEmpty());

        QVERIFY(takes.spliceRecording(recording, 0, 9000, -1,
                                      takeDirectory()).isEmpty());
        QCOMPARE(takes.unusedWrittenFiles(), QStringList { second });

        QCOMPARE(takes.removeUnusedFiles(), QStringList { second });
        QVERIFY(QFileInfo::exists(saved));
        QVERIFY(QFileInfo::exists(takes.getAudioPath()));

        // A new session starts with nothing to remember or protect
        takes.clear();
        QVERIFY(takes.getWrittenPaths().isEmpty());
        QVERIFY(takes.unusedWrittenFiles().isEmpty());
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
