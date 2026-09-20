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
#include <QFile>
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
