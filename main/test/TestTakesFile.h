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

#ifndef TEST_TAKES_FILE_H
#define TEST_TAKES_FILE_H

// Tier 2: the <takes> element of a session file, written from the takes of
// a session and read back. No window: the element is written to a string
// and to a file here, as a session's own reader would find it.

#include "../TakesFile.h"
#include "../SingingTakes.h"

#include "data/fileio/BZipFileDevice.h"

#include <QObject>
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QXmlStreamReader>

class TestTakesFile : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_fileCounter = 0;

    // The element as it appears in a session file: inside an <sv>
    // document, which is what the reading pass has to pick it out of
    static QString inDocument(QString element) {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE sonic-visualiser>\n<sv>\n<data>\n"
            "<model id=\"1\" name=\"reference\" sampleRate=\"44100\"/>\n"
            "</data>\n<display>\n<window width=\"100\" height=\"100\"/>\n"
            "</display>\n" + element + "</sv>\n";
    }

    static TakesFile::Takes readString(QString xml) {
        QXmlStreamReader reader(xml);
        return TakesFile::readFrom(reader);
    }

    // A file for the copying to work on; nothing here reads what is in it
    bool writeAudio(QString path) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        bool ok = (file.write(QByteArray(64, 'x')) == 64);
        file.close();
        return ok;
    }

    QString writeFile(QString content, bool compressed) {
        QString path = m_dir.filePath
            (QString("session-%1.ton").arg(++m_fileCounter));
        if (compressed) {
            sv::BZipFileDevice file(path);
            if (!file.open(QIODevice::WriteOnly)) return "";
            file.write(content.toUtf8());
            file.close();
        } else {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) return "";
            file.write(content.toUtf8());
            file.close();
        }
        return path;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    // Two takes and the active one, through a string and back
    void round_trip() {
        SingingTakes takes;
        QCOMPARE(takes.addTake(), QString("Take 1"));
        takes.restoreTake("C:/songs/one.wav", Coverage());
        QCOMPARE(takes.addTake(), QString("Take 2"));
        takes.restoreTake("C:/songs/two.wav", Coverage());
        QVERIFY(takes.setActiveIndex(0));

        QString element = TakesFile::toXml(takes);
        TakesFile::Takes read = readString(inDocument(element));

        QVERIFY(read.found);
        QCOMPARE(read.active, QString("Take 1"));
        QCOMPARE(int(read.takes.size()), 2);
        QCOMPARE(read.takes[0].name, QString("Take 1"));
        QCOMPARE(read.takes[0].audioPath, QString("C:/songs/one.wav"));
        QCOMPARE(read.takes[1].name, QString("Take 2"));
        QCOMPARE(read.takes[1].audioPath, QString("C:/songs/two.wav"));
    }

    // A take with no recording in it yet has no audio file
    void empty_take() {
        SingingTakes takes;
        takes.addTake();

        TakesFile::Takes read = readString(inDocument(TakesFile::toXml(takes)));

        QVERIFY(read.found);
        QCOMPARE(int(read.takes.size()), 1);
        QCOMPARE(read.takes[0].audioPath, QString());
        QCOMPARE(read.active, QString("Take 1"));
    }

    // A session with no take at all still says so, which is what tells it
    // from a session saved before the takes were stored
    void no_takes() {
        SingingTakes takes;

        TakesFile::Takes read = readString(inDocument(TakesFile::toXml(takes)));
        QVERIFY(read.found);
        QVERIFY(read.takes.empty());
        QCOMPARE(read.active, QString());

        TakesFile::Takes none = readString(inDocument(""));
        QVERIFY2(!none.found, "a document with no takes element was read as "
                 "having one");
    }

    // A name the user gave with characters that XML cares about
    void escaped_name() {
        SingingTakes takes;
        takes.addTake("Rock & \"Roll\" <2>");
        takes.restoreTake("C:/songs/a & b/take's.wav", Coverage());

        QString element = TakesFile::toXml(takes);
        QVERIFY2(!element.contains("& \""), qPrintable(element));

        TakesFile::Takes read = readString(inDocument(element));
        QCOMPARE(int(read.takes.size()), 1);
        QCOMPARE(read.takes[0].name, QString("Rock & \"Roll\" <2>"));
        QCOMPARE(read.takes[0].audioPath,
                 QString("C:/songs/a & b/take's.wav"));
        QCOMPARE(read.active, QString("Rock & \"Roll\" <2>"));
    }

    // From a file: bzip2, as a .ton is, and plain XML, which the session
    // reader also accepts
    void from_a_file() {
        SingingTakes takes;
        takes.addTake("Chorus");
        takes.restoreTake("C:/songs/chorus.wav", Coverage());

        QString document = inDocument(TakesFile::toXml(takes));

        for (bool compressed : { true, false }) {
            QString path = writeFile(document, compressed);
            QVERIFY(!path.isEmpty());
            TakesFile::Takes read = TakesFile::read(path);
            QVERIFY2(read.found, compressed ? "bzip2" : "plain");
            QCOMPARE(int(read.takes.size()), 1);
            QCOMPARE(read.takes[0].name, QString("Chorus"));
            QCOMPARE(read.takes[0].audioPath, QString("C:/songs/chorus.wav"));
        }

        // Nothing there to read
        QVERIFY(!TakesFile::read(m_dir.filePath("nothing.ton")).found);
        QVERIFY(!TakesFile::read("").found);
    }

    // The element names a take's audio relative to the session file when
    // it is in the session's own folder (spec 6.4), and where it is
    // otherwise -- before the first save, the take is in the record
    // directory
    void paths_relative_to_the_session() {
        SingingTakes takes;
        takes.addTake("Take 1");
        takes.restoreTake("C:/songs/My Song.takes/take-1.wav", Coverage());
        takes.addTake("Take 2");
        takes.restoreTake("C:/recorded/take-2.wav", Coverage());

        QString element = TakesFile::toXml(takes, "C:/songs/My Song.ton");
        TakesFile::Takes read = readString(inDocument(element));

        QCOMPARE(read.takes[0].audioPath,
                 QString("My Song.takes/take-1.wav"));
        QCOMPARE(read.takes[1].audioPath, QString("C:/recorded/take-2.wav"));

        // What a session load makes of them
        QCOMPARE(TakesFile::resolveAudioPath("C:/songs/My Song.ton",
                                             read.takes[0].audioPath),
                 QString("C:/songs/My Song.takes/take-1.wav"));

        // With no session path the paths are written as they are, which is
        // what a session saved before the folder holds
        QString absolute = TakesFile::toXml(takes);
        QCOMPARE(readString(inDocument(absolute)).takes[0].audioPath,
                 QString("C:/songs/My Song.takes/take-1.wav"));
    }

    // --- The audio folder of a session (spec 6.4) ---

    // "<session>.takes" beside the session file, whatever the session is
    // called and whichever slashes its path is written with
    void takes_folder() {
        QCOMPARE(TakesFile::takesFolder("C:/songs/My Song.ton"),
                 QString("C:/songs/My Song.takes"));
#ifdef Q_OS_WIN
        // Back slashes separate the parts of a path on Windows only;
        // elsewhere they are part of a name
        QCOMPARE(TakesFile::takesFolder("C:\\songs\\My Song.ton"),
                 QString("C:/songs/My Song.takes"));
#endif
        QCOMPARE(TakesFile::takesFolder("C:/Käännös/Säkeistö 2.ton"),
                 QString("C:/Käännös/Säkeistö 2.takes"));
        // Only the extension goes, so a name with dots of its own keeps them
        QCOMPARE(TakesFile::takesFolder("C:/songs/take.2.ton"),
                 QString("C:/songs/take.2.takes"));
        // A session file at the root of a drive, and one with no directory
        QCOMPARE(TakesFile::takesFolder("C:/song.ton"),
                 QString("C:/song.takes"));
        QCOMPARE(TakesFile::takesFolder("song.ton"), QString("song.takes"));
        QCOMPARE(TakesFile::takesFolder(""), QString());
    }

    // What goes in the file: relative to the session file when the audio is
    // inside its directory, and the path as it is when it is not
    void relative_audio_path() {
        QCOMPARE(TakesFile::relativeAudioPath
                 ("C:/songs/My Song.ton",
                  "C:/songs/My Song.takes/take-1.wav"),
                 QString("My Song.takes/take-1.wav"));
#ifdef Q_OS_WIN
        QCOMPARE(TakesFile::relativeAudioPath
                 ("C:/songs/My Song.ton",
                  "C:\\songs\\My Song.takes\\take-1.wav"),
                 QString("My Song.takes/take-1.wav"));
#endif
        QCOMPARE(TakesFile::relativeAudioPath
                 ("C:/Käännös/Säkeistö.ton",
                  "C:/Käännös/Säkeistö.takes/take-1.wav"),
                 QString("Säkeistö.takes/take-1.wav"));

        // Not below the session: the record directory before the first
        // save, or another drive altogether
        QCOMPARE(TakesFile::relativeAudioPath
                 ("C:/songs/My Song.ton", "C:/recorded/take-1.wav"),
                 QString("C:/recorded/take-1.wav"));
        QCOMPARE(TakesFile::relativeAudioPath
                 ("C:/songs/My Song.ton", "D:/songs/My Song.takes/take-1.wav"),
                 QString("D:/songs/My Song.takes/take-1.wav"));

        // A take with no audio in it yet, and no session to be relative to
        QCOMPARE(TakesFile::relativeAudioPath("C:/songs/My Song.ton", ""),
                 QString());
        QCOMPARE(TakesFile::relativeAudioPath("", "C:/recorded/take-1.wav"),
                 QString("C:/recorded/take-1.wav"));

        // A session file at the root of a drive
        QCOMPARE(TakesFile::relativeAudioPath
                 ("C:/song.ton", "C:/song.takes/take-1.wav"),
                 QString("song.takes/take-1.wav"));
    }

    // And back: a relative path against the session's directory, an
    // absolute one -- as a session saved before the folder has -- as it is
    void resolve_audio_path() {
        QCOMPARE(TakesFile::resolveAudioPath
                 ("C:/songs/My Song.ton", "My Song.takes/take-1.wav"),
                 QString("C:/songs/My Song.takes/take-1.wav"));
#ifdef Q_OS_WIN
        QCOMPARE(TakesFile::resolveAudioPath
                 ("C:\\songs\\My Song.ton", "My Song.takes\\take-1.wav"),
                 QString("C:/songs/My Song.takes/take-1.wav"));
#endif
        QCOMPARE(TakesFile::resolveAudioPath
                 ("C:/Käännös/Säkeistö.ton", "Säkeistö.takes/take-1.wav"),
                 QString("C:/Käännös/Säkeistö.takes/take-1.wav"));
        // An absolute path is one in the form of the system the session
        // is read on: a drive letter makes one only on Windows
#ifdef Q_OS_WIN
        QString elsewhere = "C:/recorded/take-1.wav";
#else
        QString elsewhere = "/recorded/take-1.wav";
#endif
        QCOMPARE(TakesFile::resolveAudioPath("C:/songs/My Song.ton", elsewhere),
                 elsewhere);
        QCOMPARE(TakesFile::resolveAudioPath("C:/songs/My Song.ton", ""),
                 QString());

        // The session moved, with its folder: the same relative path finds
        // the audio in the new place
        QCOMPARE(TakesFile::resolveAudioPath
                 ("D:/backup/songs/My Song.ton", "My Song.takes/take-1.wav"),
                 QString("D:/backup/songs/My Song.takes/take-1.wav"));

        // Written and read back, for every shape of path
        for (QString session : { "C:/songs/My Song.ton", "C:/song.ton",
                                 "C:/Käännös/Säkeistö 2.ton" }) {
            for (QString audio : { "take-1.wav", "deeper/take-2.wav" }) {
                QString path = QDir::cleanPath
                    (TakesFile::takesFolder(session) + "/" + audio);
                QString stored = TakesFile::relativeAudioPath(session, path);
                QVERIFY2(!QFileInfo(stored).isAbsolute(), qPrintable(stored));
                QCOMPARE(TakesFile::resolveAudioPath(session, stored), path);
            }
        }
    }

    void in_folder() {
        QVERIFY(TakesFile::isInFolder("C:/songs/My Song.takes",
                                      "C:/songs/My Song.takes/take-1.wav"));
#ifdef Q_OS_WIN
        // Windows tells no two names apart by their case
        QVERIFY(TakesFile::isInFolder("C:/songs/my song.takes",
                                      "C:\\Songs\\My Song.takes\\take-1.wav"));
#else
        // Other systems do
        QVERIFY(!TakesFile::isInFolder("/songs/my song.takes",
                                       "/Songs/My Song.takes/take-1.wav"));
#endif
        QVERIFY(TakesFile::isInFolder("C:/songs/My Song.takes/",
                                      "C:/songs/My Song.takes/in/take-1.wav"));
        QVERIFY(!TakesFile::isInFolder("C:/songs/My Song.takes",
                                       "C:/songs/My Song.takes"));
        QVERIFY(!TakesFile::isInFolder("C:/songs/My Song.takes",
                                       "C:/songs/My Song.takes-2/take-1.wav"));
        QVERIFY(!TakesFile::isInFolder("C:/songs/My Song.takes",
                                       "C:/recorded/take-1.wav"));
        QVERIFY(!TakesFile::isInFolder("", "C:/recorded/take-1.wav"));
        QVERIFY(!TakesFile::isInFolder("C:/songs/My Song.takes", ""));
    }

    // A copy is never written over a file that is there already
    void free_copy_path() {
        QString folder = m_dir.filePath("free");
        QVERIFY(QDir().mkpath(folder));

        QString first = TakesFile::freeCopyPath(folder, "take-1.wav");
        QCOMPARE(first, QDir::cleanPath(folder + "/take-1.wav"));

        QVERIFY(writeAudio(first));
        QString second = TakesFile::freeCopyPath(folder, "take-1.wav");
        QCOMPARE(second, QDir::cleanPath(folder + "/take-1-2.wav"));

        QVERIFY(writeAudio(second));
        QCOMPARE(TakesFile::freeCopyPath(folder, "take-1.wav"),
                 QDir::cleanPath(folder + "/take-1-3.wav"));

        QCOMPARE(TakesFile::freeCopyPath("", "take-1.wav"), QString());
        QCOMPARE(TakesFile::freeCopyPath(folder, ""), QString());
    }

    // The copy a save makes: one per file, the takes pointing at the
    // copies, and the copies counted as files this run wrote
    void copy_take_audio_into_the_folder() {
        QString record = m_dir.filePath("rec");
        QString folder = m_dir.filePath("copied.takes");
        QVERIFY(QDir().mkpath(record));
        QVERIFY(QDir().mkpath(folder));

        QString one = QDir::cleanPath(record + "/take-1.wav");
        QString two = QDir::cleanPath(record + "/take-2.wav");
        QString inside = QDir::cleanPath(folder + "/take-3.wav");
        QVERIFY(writeAudio(one));
        QVERIFY(writeAudio(two));
        QVERIFY(writeAudio(inside));

        SingingTakes takes;
        takes.addTake("A");
        takes.restoreTake(one, Coverage());
        // Two takes sharing one file, as a duplicated take does
        takes.addTake("B");
        takes.restoreTake(one, Coverage());
        takes.addTake("C");
        takes.restoreTake(two, Coverage());
        // And one whose audio is in the folder already
        takes.addTake("D");
        takes.restoreTake(inside, Coverage());

        QCOMPARE(TakesFile::copyTakeAudioInto(takes, folder), QString());

        QString copyOfOne = QDir::cleanPath(folder + "/take-1.wav");
        QString copyOfTwo = QDir::cleanPath(folder + "/take-2.wav");
        QCOMPARE(takes.getTake(0)->audioPath, copyOfOne);
        QCOMPARE(takes.getTake(1)->audioPath, copyOfOne);
        QCOMPARE(takes.getTake(2)->audioPath, copyOfTwo);
        QCOMPARE(takes.getTake(3)->audioPath, inside);
        QVERIFY(QFileInfo::exists(copyOfOne));
        QVERIFY(QFileInfo::exists(copyOfTwo));

        // The shared file was copied once: a second copy would have been
        // "take-1-2.wav"
        QVERIFY2(!QFileInfo::exists(QDir::cleanPath(folder + "/take-1-2.wav")),
                 "a file two takes share was copied twice");

        // The originals are still there, for the models that have them open
        QVERIFY(QFileInfo::exists(one));
        QVERIFY(QFileInfo::exists(two));

        // The copies are ours, so the cleanup on close may take them away
        // again if no saved session names them
        QCOMPARE(takes.getWrittenPaths(), QStringList({ copyOfOne, copyOfTwo }));

        // Saving again copies nothing: everything is in the folder
        QCOMPARE(TakesFile::copyTakeAudioInto(takes, folder), QString());
        QCOMPARE(takes.getTake(0)->audioPath, copyOfOne);
        QCOMPARE(takes.getWrittenPaths(), QStringList({ copyOfOne, copyOfTwo }));
    }

    // A copy that fails leaves the takes exactly as they were, and takes
    // the copies that were made with it: better no saved session than one
    // that names files which are not there
    void a_failed_copy_changes_nothing() {
        QString record = m_dir.filePath("rec-failing");
        QVERIFY(QDir().mkpath(record));

        QString one = QDir::cleanPath(record + "/take-1.wav");
        QVERIFY(writeAudio(one));
        QString missing = QDir::cleanPath(record + "/not-there.wav");

        SingingTakes takes;
        takes.addTake("A");
        takes.restoreTake(one, Coverage());
        takes.addTake("B");
        takes.restoreTake(missing, Coverage());

        // No such folder to copy into
        QString folder = m_dir.filePath("nowhere.takes");
        QString error = TakesFile::copyTakeAudioInto(takes, folder);
        QVERIFY2(error != "", "copying into a folder that is not there "
                 "reported no error");
        QCOMPARE(takes.getTake(0)->audioPath, one);
        QCOMPARE(takes.getTake(1)->audioPath, missing);
        QVERIFY(takes.getWrittenPaths().isEmpty());

        // The folder is there, but the second take's audio is not: the
        // first take's copy is removed again
        QVERIFY(QDir().mkpath(folder));
        error = TakesFile::copyTakeAudioInto(takes, folder);
        QVERIFY2(error != "", "copying a file that is not there reported no "
                 "error");
        QCOMPARE(takes.getTake(0)->audioPath, one);
        QCOMPARE(takes.getTake(1)->audioPath, missing);
        QVERIFY(takes.getWrittenPaths().isEmpty());
        QVERIFY2(!QFileInfo::exists(QDir::cleanPath(folder + "/take-1.wav")),
                 "a copy made before the failure was left behind");

        QVERIFY(TakesFile::copyTakeAudioInto(takes, "") != "");
    }

    // The numbering carries on from the takes a session had, whatever has
    // happened to them since
    void reserved_names_carry_the_numbering_on() {
        SingingTakes takes;
        takes.reserveTakeName("Take 1");
        takes.reserveTakeName("Take 7");
        takes.reserveTakeName("Chorus");

        QCOMPARE(takes.addTake("Take 7"), QString("Take 7"));
        QCOMPARE(takes.addTake(), QString("Take 8"));
    }
};

#endif
