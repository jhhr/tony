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

#ifndef TEST_VOICE_TRACKS_H
#define TEST_VOICE_TRACKS_H

// Tier 2: the reference's voice tracks, as an analysis's outputs give
// them and as a session keeps them in its <voicetracks> element, read in
// the same pass as its <analysis> element. No window

#include "../VoiceTracks.h"
#include "../AnalysisSettings.h"
#include "../SessionXml.h"
#include "../SingingTakes.h"
#include "../TakesFile.h"

#include "data/fileio/BZipFileDevice.h"

#include <QObject>
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QXmlStreamReader>

class TestVoiceTracks : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_fileCounter = 0;

    typedef VoiceTracks::Track Track;

    // Three tracks as a harmony gives them: the same outside 1000-2000,
    // the lower and the higher on their voices inside it, the strongest
    // on one, then the other
    static VoiceTracks harmony() {
        Track strongest, lower, higher;
        for (sv::sv_frame_t f = 0; f < 3000; f += 256) {
            const bool both = (f >= 1000 && f < 2000);
            strongest.push_back({ f, both && f < 1500 ? 277.18 : 220.25 });
            lower.push_back({ f, 220.25 });
            higher.push_back({ f, both ? 277.18 : 220.25 });
        }
        VoiceTracks tracks;
        tracks.set(VoiceTracks::Strongest, strongest);
        tracks.set(VoiceTracks::Lower, lower);
        tracks.set(VoiceTracks::Higher, higher);
        return tracks;
    }

    static QString inDocument(QString elements) {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE sonic-visualiser>\n<sv>\n<data>\n"
            "<model id=\"1\" name=\"reference\" sampleRate=\"44100\"/>\n"
            "</data>\n" + elements + "</sv>\n";
    }

    static VoiceTracks readString(QString xml) {
        QXmlStreamReader reader(xml);
        VoiceTracks tracks;
        while (!reader.atEnd()) {
            if (reader.readNext() == QXmlStreamReader::StartElement &&
                reader.name().toString() == "voicetracks") {
                tracks = VoiceTracks::fromElement(reader);
            }
        }
        return tracks;
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

    // What an analysis's output gives: in frame order, the unvoiced
    // frames left out, and the one frame fixed-lag decoding gives twice
    // kept once, as it came first
    void set_keeps_voiced_frames_once() {
        Track points { { 0, 220.0 }, { 256, -220.0 }, { 512, 0.0 },
                       { 768, 221.0 }, { 768, 222.0 }, { 1024, 223.0 } };
        VoiceTracks tracks;
        tracks.set(VoiceTracks::Lower, points);
        const Track expected { { 0, 220.0 }, { 768, 221.0 }, { 1024, 223.0 } };
        QVERIFY(tracks.get(VoiceTracks::Lower) == expected);
        QVERIFY(!tracks.isComplete());
        tracks.set(VoiceTracks::Strongest, {});
        tracks.set(VoiceTracks::Higher, points);
        QVERIFY2(tracks.isComplete(), "an empty track is a track: a song "
                 "with no voice found in it");
        tracks.clear();
        QVERIFY(!tracks.isComplete());
        QVERIFY(tracks.get(VoiceTracks::Higher).empty());
    }

    // A range from its start up to its end, not including it
    void within_a_range() {
        VoiceTracks tracks = harmony();
        Track t = tracks.within(VoiceTracks::Higher, 1024, 2048);
        QCOMPARE(int(t.size()), 4);
        QCOMPARE(t.front().frame, sv::sv_frame_t(1024));
        QCOMPARE(t.back().frame, sv::sv_frame_t(1792));
        for (const auto &p : t) QCOMPARE(p.hz, 277.18);
        QVERIFY(tracks.within(VoiceTracks::Lower, 2900, 2900).empty());
        QVERIFY(tracks.within(VoiceTracks::Lower, 5000, 6000).empty());
        QCOMPARE(int(tracks.within(VoiceTracks::Lower, -100, 1).size()), 1);
    }

    // Through the element and back, each voice whole
    void round_trip() {
        VoiceTracks tracks = harmony();
        QString element = tracks.toXml();
        QVERIFY(element.startsWith("  <voicetracks>\n"));
        QVERIFY(element.endsWith("  </voicetracks>\n"));
        VoiceTracks read = readString(inDocument(element));
        QVERIFY(read.isComplete());
        QVERIFY2(read == tracks, qPrintable(element.left(400)));
    }

    // Seven significant figures: the pitch back within a hundredth of a
    // cent
    void pitch_kept_to_a_hundredth_of_a_cent() {
        VoiceTracks tracks;
        const double hz = 220.123456789;
        for (VoiceTracks::Voice v : { VoiceTracks::Strongest, VoiceTracks::Lower,
                                      VoiceTracks::Higher }) {
            tracks.set(v, { { 512, hz } });
        }
        VoiceTracks read = readString(inDocument(tracks.toXml()));
        QVERIFY(read.isComplete());
        const double cents =
            1200 * std::log2(read.get(VoiceTracks::Lower).front().hz / hz);
        QVERIFY2(std::abs(cents) < 0.01, qPrintable(QString::number(cents)));
    }

    // Not written without all three voices, nor read: a session can use
    // nothing less
    void incomplete_is_neither_written_nor_read() {
        VoiceTracks tracks;
        tracks.set(VoiceTracks::Lower, { { 0, 220.0 } });
        QCOMPARE(tracks.toXml(), QString());

        VoiceTracks read = readString(inDocument
            ("<voicetracks>\n<voice name=\"strongest\">0 220</voice>\n"
             "<voice name=\"lower\">0 220</voice>\n</voicetracks>\n"));
        QVERIFY(!read.isComplete());

        // Text that is not frames and pitches in pairs is no track
        read = readString(inDocument
            ("<voicetracks>\n<voice name=\"strongest\">0 220</voice>\n"
             "<voice name=\"lower\">0 220 256</voice>\n"
             "<voice name=\"higher\">0 220</voice>\n</voicetracks>\n"));
        QVERIFY(!read.isComplete());
        read = readString(inDocument
            ("<voicetracks>\n<voice name=\"strongest\">0 220</voice>\n"
             "<voice name=\"lower\">0 220</voice>\n"
             "<voice name=\"higher\">zero 220</voice>\n</voicetracks>\n"));
        QVERIFY(!read.isComplete());

        // and a voice of no name of ours is passed over
        read = readString(inDocument
            ("<voicetracks>\n<voice name=\"strongest\"></voice>\n"
             "<voice name=\"lower\">0 220</voice>\n"
             "<voice name=\"middle\">0 250</voice>\n"
             "<voice name=\"higher\">0 260</voice>\n</voicetracks>\n"));
        QVERIFY(read.isComplete());
        QVERIFY(read.get(VoiceTracks::Strongest).empty());
        QCOMPARE(read.get(VoiceTracks::Higher).front().hz, 260.0);
    }

    // A session's own elements read in one pass, whatever their order,
    // bzip2 as a .ton is and plain: the analysis options and the voice
    // tracks, with the takes beside them
    void one_pass_reads_both_elements() {
        SingingTakes takes;
        takes.addTake("Verse");
        const AnalysisSettings::Values defaults {
            { "precision-analysis", false }, { "voice-to-follow", 0 } };
        const AnalysisSettings::Values chosen {
            { "precision-analysis", true }, { "voice-to-follow", 1 } };
        VoiceTracks tracks = harmony();

        for (bool voicesFirst : { false, true }) {
            QString elements = voicesFirst ?
                tracks.toXml() + AnalysisSettings::toXml(chosen) :
                AnalysisSettings::toXml(chosen) + tracks.toXml();
            QString document = inDocument(TakesFile::toXml(takes) + elements);
            for (bool compressed : { true, false }) {
                QString path = writeFile(document, compressed);
                QVERIFY(!path.isEmpty());
                AnalysisSettings::Stored analysis;
                VoiceTracks voices;
                QVERIFY(SessionXml::readElements(path, {
                    { "analysis", [&](QXmlStreamReader &r) {
                        analysis = AnalysisSettings::fromElement(r, defaults);
                    } },
                    { "voicetracks", [&](QXmlStreamReader &r) {
                        voices = VoiceTracks::fromElement(r);
                    } } }));
                QVERIFY(analysis.found);
                QVERIFY(analysis.values == chosen);
                QVERIFY2(voices == tracks,
                         compressed ? "bzip2" : "plain");
                QCOMPARE(int(TakesFile::read(path).takes.size()), 1);
            }
        }

        QVERIFY(!SessionXml::readElements(m_dir.filePath("nothing.ton"), {}));
    }
};

#endif
