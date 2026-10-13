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

#ifndef TEST_ANALYSIS_SETTINGS_H
#define TEST_ANALYSIS_SETTINGS_H

// Tier 2: the analysis options a session keeps, in its <analysis> element,
// and the user's own, in the settings. No window: the element is written
// to a string and to a file here, as a session's own reader would find it.

#include "../AnalysisSettings.h"
#include "../SingingTakes.h"
#include "../TakesFile.h"

#include "data/fileio/BZipFileDevice.h"

#include <QObject>
#include <QtTest>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QXmlStreamReader>

class TestAnalysisSettings : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_fileCounter = 0;

    // Options of both kinds the Analysis menu has, a truth value and a
    // choice of a number, with their defaults
    static AnalysisSettings::Values defaults() {
        return { { "precision-analysis", false },
                 { "prune-analysis", true },
                 { "voice-to-follow", 0 } };
    }

    // The element as it appears in a session file: inside an <sv>
    // document, which is what the reading pass has to pick it out of
    static QString inDocument(QString element) {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE sonic-visualiser>\n<sv>\n<data>\n"
            "<model id=\"1\" name=\"reference\" sampleRate=\"44100\"/>\n"
            "</data>\n<display>\n<window width=\"100\" height=\"100\"/>\n"
            "</display>\n" + element + "</sv>\n";
    }

    static AnalysisSettings::Stored readString(QString xml) {
        QXmlStreamReader reader(xml);
        return AnalysisSettings::readFrom(reader, defaults());
    }

    // The value and its type both: a truth value read back as text, or a
    // number as a truth value, would be another value to the menu
    static bool same(const AnalysisSettings::Values &a,
                     const AnalysisSettings::Values &b) {
        if (a.size() != b.size()) return false;
        for (const auto &v : a) {
            auto it = b.find(v.first);
            if (it == b.end()) return false;
            if (it->second.typeId() != v.second.typeId()) return false;
            if (it->second != v.second) return false;
        }
        return true;
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

    // Every option, none of them at its default, through a string and
    // back, each as the type of its default
    void round_trip() {
        const AnalysisSettings::Values chosen {
            { "precision-analysis", true },
            { "prune-analysis", false },
            { "voice-to-follow", 1 } };

        QString element = AnalysisSettings::toXml(chosen);
        QVERIFY2(element.startsWith("  <analysis ") && element.endsWith("/>\n"),
                 qPrintable(element));

        AnalysisSettings::Stored read = readString(inDocument(element));
        QVERIFY(read.found);
        QVERIFY2(same(read.values, chosen), qPrintable(element));
    }

    // No element: a session whose analysis is not known. The values are
    // the defaults, but found says they are not the session's
    void no_element() {
        AnalysisSettings::Stored read = readString(inDocument(""));
        QVERIFY2(!read.found, "a document with no analysis element was read "
                 "as having one");
        QVERIFY(same(read.values, defaults()));
    }

    // An option the element does not have is at its default, and one it
    // has that is no option of ours is passed over
    void missing_and_unknown_options() {
        AnalysisSettings::Stored read = readString(inDocument
            ("<analysis voice-to-follow=\"1\" colour=\"blue\"/>\n"));
        QVERIFY(read.found);
        const AnalysisSettings::Values expected {
            { "precision-analysis", false },
            { "prune-analysis", true },
            { "voice-to-follow", 1 } };
        QVERIFY(same(read.values, expected));
    }

    // A file anybody may have edited: a truth value is "true", "false",
    // 1 or 0, in any case, and anything else, as a number that is no
    // number, is the default
    void values_of_another_kind() {
        AnalysisSettings::Stored read = readString(inDocument
            ("<analysis precision-analysis=\"TRUE\" prune-analysis=\"0\" "
             "voice-to-follow=\"lower\"/>\n"));
        QVERIFY(read.found);
        QCOMPARE(read.values["precision-analysis"], QVariant(true));
        QCOMPARE(read.values["prune-analysis"], QVariant(false));
        QCOMPARE(read.values["voice-to-follow"], QVariant(0));

        read = readString(inDocument
            ("<analysis precision-analysis=\"yes\" prune-analysis=\"\"/>\n"));
        QCOMPARE(read.values["precision-analysis"], QVariant(false));
        QCOMPARE(read.values["prune-analysis"], QVariant(true));
    }

    // From a file, bzip2 as a .ton is and plain XML, with the takes beside
    // it as a session has them: each reading pass finds its own element
    void from_a_file() {
        SingingTakes takes;
        takes.addTake("Chorus");
        const AnalysisSettings::Values chosen {
            { "precision-analysis", true },
            { "prune-analysis", false },
            { "voice-to-follow", 1 } };

        QString document = inDocument(TakesFile::toXml(takes) +
                                      AnalysisSettings::toXml(chosen));

        for (bool compressed : { true, false }) {
            QString path = writeFile(document, compressed);
            QVERIFY(!path.isEmpty());
            AnalysisSettings::Stored read =
                AnalysisSettings::read(path, defaults());
            QVERIFY2(read.found, compressed ? "bzip2" : "plain");
            QVERIFY(same(read.values, chosen));
            QCOMPARE(int(TakesFile::read(path).takes.size()), 1);
        }

        // Nothing there to read
        AnalysisSettings::Stored none =
            AnalysisSettings::read(m_dir.filePath("nothing.ton"), defaults());
        QVERIFY(!none.found);
        QVERIFY(same(none.values, defaults()));
        QVERIFY(!AnalysisSettings::read("", defaults()).found);
    }

    // The user's own, from the group "Analyser" of the settings: written
    // there as the menu writes them, or as text, as an INI file keeps
    // them; an option never chosen is at its default
    void from_the_settings() {
        QSettings settings;
        settings.beginGroup("Analyser");
        settings.setValue("precision-analysis", "true");
        settings.setValue("voice-to-follow", 1);
        settings.remove("prune-analysis");
        settings.endGroup();

        AnalysisSettings::Values read =
            AnalysisSettings::fromSettings(settings, defaults());
        const AnalysisSettings::Values expected {
            { "precision-analysis", true },
            { "prune-analysis", true },
            { "voice-to-follow", 1 } };
        QVERIFY(same(read, expected));

        settings.beginGroup("Analyser");
        settings.remove("precision-analysis");
        settings.remove("voice-to-follow");
        settings.endGroup();
        QVERIFY(same(AnalysisSettings::fromSettings(settings, defaults()),
                     defaults()));
    }
};

#endif
