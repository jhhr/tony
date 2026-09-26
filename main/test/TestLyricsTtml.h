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

#ifndef TEST_LYRICS_TTML_H
#define TEST_LYRICS_TTML_H

// Tier 2: reading and writing TTML lyrics, and parseLyrics() choosing
// between TTML and LRC. No window and no model. Two of the files in
// testdata/lyrics were written by the Moises-Lyric-Exporter's own TTML
// code, run on an invented lyrics.json; the third is written by hand
// as AMLL TTML Tool writes its files.

#include "../Lyrics.h"
#include "../LyricsTtml.h"

#include <QObject>
#include <QtTest>
#include <QFile>
#include <QStringConverter>

#include <cmath>

class TestLyricsTtml : public QObject
{
    Q_OBJECT

    // The inferred lengths, as the parser has them
    static constexpr double W = Lyrics::inferredWordSeconds;
    static constexpr double L = Lyrics::inferredLastLineSeconds;

    // A whole file around the given <div> content, and head metadata
    static QByteArray ttml(const char *div, const char *metadata = "") {
        return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<tt xmlns=\"http://www.w3.org/ns/ttml\" "
                          "xmlns:ttm=\"http://www.w3.org/ns/ttml#metadata\">"
                          "<head><metadata>") + metadata +
            "</metadata></head><body><div>" + div + "</div></body></tt>\n";
    }

    static LyricsParseResult parse(const char *div) {
        return parseTtml(ttml(div));
    }

    static QByteArray fixture(const char *name) {
        QFile file(QString(TONY_TEST_DATA_DIR) + "/lyrics/" + name);
        if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return file.readAll();
    }

    static int wordCount(const LyricsParseResult &r) {
        return int(r.lyrics.words.size());
    }

    struct Expected {
        double start;
        double end;
        const char *text;
        int line;
        bool endGiven;
    };

    static QString describe(const LyricWord &w) {
        return QString("\"%1\" %2-%3 line %4%5")
            .arg(w.text).arg(w.start, 0, 'f', 6).arg(w.end, 0, 'f', 6)
            .arg(w.line).arg(w.endGiven ? ", end given" : "");
    }

    static QString describe(const Lyrics &lyrics) {
        QStringList list;
        for (const LyricWord &w : lyrics.words) list << describe(w);
        return list.join("; ");
    }

    // Times of whole milliseconds are read as the nearest double to
    // the decimal: this only absorbs the last bit of one
    static bool sameTime(double a, double b) {
        return std::abs(a - b) < 1e-9;
    }

    // All the words, in order. A failure names the word that differs.
    static void compareWords(const Lyrics &lyrics,
                             const QVector<Expected> &expected) {
        QVERIFY2(lyrics.words.size() == expected.size(),
                 qPrintable(QString("%1 words, expected %2: %3")
                            .arg(lyrics.words.size()).arg(expected.size())
                            .arg(describe(lyrics))));
        for (int i = 0; i < expected.size(); ++i) {
            const LyricWord &w = lyrics.words[i];
            LyricWord want;
            want.start = expected[i].start;
            want.end = expected[i].end;
            want.text = QString::fromUtf8(expected[i].text);
            want.line = expected[i].line;
            want.endGiven = expected[i].endGiven;
            bool same = (w.text == want.text &&
                         sameTime(w.start, want.start) &&
                         sameTime(w.end, want.end) &&
                         w.line == want.line &&
                         w.endGiven == want.endGiven);
            QVERIFY2(same, qPrintable(QString("word %1 is %2, expected %3")
                                      .arg(i).arg(describe(w))
                                      .arg(describe(want))));
        }
    }

    // One warning, starting with the count it should give
    static void verifyOneWarning(const LyricsParseResult &r,
                                 const QString &count) {
        QVERIFY2(r.warnings.size() == 1 && r.warnings[0].startsWith(count),
                 qPrintable(QString("warnings: [%1], expected one "
                                    "starting \"%2\"")
                            .arg(r.warnings.join(" | ")).arg(count)));
    }

    // Back from TTML, the same words, texts and lines, each time
    // within half a millisecond
    static void verifyRoundTrip(const Lyrics &lyrics, const char *what) {
        const QByteArray written = writeTtml(lyrics);
        LyricsParseResult r = parseTtml(written);
        QVERIFY2(r.error.isEmpty(),
                 qPrintable(QString("%1: %2").arg(what).arg(r.error)));
        QVERIFY2(r.warnings.isEmpty(),
                 qPrintable(QString("%1: %2").arg(what)
                            .arg(r.warnings.join(" | "))));
        QCOMPARE(r.lyrics.title, lyrics.title);
        QVERIFY(r.lyrics.wordTimed);
        QVERIFY2(r.lyrics.words.size() == lyrics.words.size(),
                 qPrintable(QString("%1: %2 words back, expected %3: %4")
                            .arg(what).arg(r.lyrics.words.size())
                            .arg(lyrics.words.size())
                            .arg(describe(r.lyrics))));
        const double halfMs = 0.0005 + 1e-9;
        for (int i = 0; i < lyrics.words.size(); ++i) {
            const LyricWord &w = r.lyrics.words[i];
            const LyricWord &o = lyrics.words[i];
            bool same = (w.text == o.text && w.line == o.line &&
                         std::abs(w.start - o.start) <= halfMs &&
                         std::abs(w.end - o.end) <= halfMs);
            QVERIFY2(same, qPrintable(QString("%1: word %2 came back as "
                                              "%3, was %4")
                                      .arg(what).arg(i).arg(describe(w))
                                      .arg(describe(o))));
        }
        // and writing it again gives the same file
        QCOMPARE(writeTtml(r.lyrics), written);
    }

private slots:
    // Clock times with and without hours, minutes past 59, plain
    // seconds and offset times, all read as absolute
    void times_in_every_accepted_form() {
        struct Case {
            const char *time;
            double seconds;
        } cases[] = {
            { "0:01.000", 1.0 },            // the exporter
            { "1:02.500", 62.5 },
            { "01:02.500", 62.5 },          // AMLL TTML Tool
            { "1:02.5", 62.5 },
            { "1:02.50", 62.5 },
            { "1:02", 62.0 },
            { "1:2.25", 62.25 },
            { "75:00.000", 4500.0 },        // minutes past 59, no hours
            { "1:01:02.5", 3662.5 },
            { "01:01:02.250", 3662.25 },
            { "0:00:00.001", 0.001 },
            { "62.5", 62.5 },               // plain seconds
            { "62", 62.0 },
            { "4500.25", 4500.25 },
            { "0", 0.0 },
            { "62.5s", 62.5 },              // offset times
            { "62500ms", 62.5 },
            { "250.5ms", 0.2505 },
            { "1.5m", 90.0 },
            { "2m", 120.0 },
            { "0.5h", 1800.0 },
            { " 1:02.500 ", 62.5 },
            { "1:02.1234567", 62.123456 },  // microseconds are plenty
            { "24:00:00", 86400.0 },        // a day is the most
        };
        for (const Case &c : cases) {
            QByteArray div = QByteArray("<p><span begin=\"") + c.time +
                "\" end=\"" + c.time + "\">sana</span></p>";
            LyricsParseResult r = parseTtml(ttml(div.constData()));
            QVERIFY2(r.error.isEmpty() && r.warnings.isEmpty(),
                     qPrintable(QString("\"%1\": %2 %3").arg(c.time)
                                .arg(r.error).arg(r.warnings.join(" "))));
            QCOMPARE(wordCount(r), 1);
            QVERIFY2(sameTime(r.lyrics.words[0].start, c.seconds) &&
                     sameTime(r.lyrics.words[0].end, c.seconds),
                     qPrintable(QString("\"%1\" read as %2")
                                .arg(c.time)
                                .arg(describe(r.lyrics.words[0]))));
        }
    }

    // Frames, ticks and anything else that is no time: the word is
    // left out, with a warning, and the rest of the line is read
    void refused_times() {
        const char *times[] = {
            "00:00:01:12",      // hh:mm:ss:ff
            "25f", "100t",      // frames and ticks
            "1:60.000", "1:60:00", "1:02:60",
            "-1.0", "", "  ", "abc", "1.2.3", "1:02.", ".5", "1e3",
            "12 s", "1:02.5s", "1::02", ":02", "1:",
            "24:00:00.001", "25:00:00", "1441m",
            "99999999999999999999",
        };
        for (const char *time : times) {
            QByteArray div = QByteArray("<p><span begin=\"") + time +
                "\" end=\"0:05.000\">ei</span> "
                "<span begin=\"0:01.000\" end=\"0:02.000\">kyllä</span></p>";
            LyricsParseResult r = parseTtml(ttml(div.constData()));
            QVERIFY2(r.error.isEmpty(), time);
            compareWords(r.lyrics, { { 1.0, 2.0, "kyllä", 0, true } });
            verifyOneWarning(r, "1 word had a start time");
            if (QTest::currentTestFailed()) {
                qWarning() << "time:" << time;
                return;
            }

            // A line-timed line likewise
            div = QByteArray("<p begin=\"") + time + "\" end=\"0:05.000\">"
                "ei</p><p begin=\"0:01.000\" end=\"0:02.000\">kyllä</p>";
            r = parseTtml(ttml(div.constData()));
            QVERIFY2(r.error.isEmpty(), time);
            compareWords(r.lyrics, { { 1.0, 2.0, "kyllä", 0, true } });
            verifyOneWarning(r, "1 word had a start time");
            if (QTest::currentTestFailed()) {
                qWarning() << "line time:" << time;
                return;
            }
        }
    }

    // A word ends at its end, or else at its begin + dur; an end that
    // cannot be read is ignored, with one warning for all of them
    void end_or_dur() {
        LyricsParseResult r = parse
            ("<p><span begin=\"1.0\" end=\"1.5\">a</span> "
             "<span begin=\"2.0\" dur=\"0.25\">b</span> "
             "<span begin=\"3.0\" end=\"3.5\" dur=\"9\">c</span> "
             "<span begin=\"4.0\" dur=\"250ms\">d</span> "
             "<span begin=\"5.0\" end=\"väärä\" dur=\"0.5\">e</span> "
             "<span begin=\"6.0\" end=\"x\">f</span></p>");
        QCOMPARE(r.error, QString());
        compareWords(r.lyrics, {
                { 1.0, 1.5, "a", 0, true },
                { 2.0, 2.25, "b", 0, true },
                { 3.0, 3.5, "c", 0, true },
                { 4.0, 4.25, "d", 0, true },
                { 5.0, 5.5, "e", 0, true },
                { 6.0, 6.0 + W, "f", 0, false },
            });
        verifyOneWarning(r, "2 ");
    }

    // Timed spans with no white space between them are one word:
    // first start, last end, the texts together
    void syllables_joined() {
        LyricsParseResult r = parse
            ("<p begin=\"1.0\" end=\"3.0\">"
             "<span begin=\"1.0\" end=\"1.2\">kek</span>"
             "<span begin=\"1.2\" end=\"1.5\">sit</span>"
             "<span begin=\"1.5\" end=\"2.0\">ty</span> "
             "<span begin=\"2.1\" end=\"3.0\">laulu</span></p>");
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        QVERIFY(r.lyrics.wordTimed);
        compareWords(r.lyrics, {
                { 1.0, 2.0, "keksitty", 0, true },
                { 2.1, 3.0, "laulu", 0, true },
            });
        if (QTest::currentTestFailed()) return;

        // The end is the last syllable's: if it has none, the word's
        // end is inferred
        r = parse("<p><span begin=\"1.0\" end=\"1.2\">la</span>"
                  "<span begin=\"1.2\">lu</span> "
                  "<span begin=\"2.0\" end=\"2.5\">x</span></p>");
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 1.0, 2.0, "lalu", 0, false },
                { 2.0, 2.5, "x", 0, true },
            });
    }

    // White space separates words wherever it is: line breaks and
    // indentation of a laid-out file, CRLF, tabs, <br/>, and blanks
    // inside a span at either end of its text
    void whitespace_separates_words() {
        LyricsParseResult r = parse
            ("\n      <p begin=\"1.0\" end=\"4.5\">\r\n"
             "        <span begin=\"1.0\" end=\"1.5\">yksi</span>\r\n"
             "        <span begin=\"1.5\" end=\"2.0\">kaksi</span><br/>"
             "<span begin=\"2.0\" end=\"2.5\">kolme</span>&#9;"
             "<span begin=\"2.5\" end=\"3.0\">neljä</span>"
             "<span begin=\"3.0\" end=\"3.5\"> viisi</span> "
             "<span begin=\"3.5\" end=\"4.0\">kuusi\n</span>"
             "<span begin=\"4.0\" end=\"4.5\">seitsemän</span>\n"
             "      </p>\n");
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 1.0, 1.5, "yksi", 0, true },
                { 1.5, 2.0, "kaksi", 0, true },
                { 2.0, 2.5, "kolme", 0, true },
                { 2.5, 3.0, "neljä", 0, true },
                { 3.0, 3.5, "viisi", 0, true },
                { 3.5, 4.0, "kuusi", 0, true },
                { 4.0, 4.5, "seitsemän", 0, true },
            });
    }

    // Text straight after a word is part of it; other text in a line
    // of timed words is left out, with a warning
    void punctuation_appended() {
        LyricsParseResult r = parse
            ("<p><span begin=\"1.0\" end=\"1.5\">sana</span>, "
             "<span begin=\"2.0\" end=\"2.5\">toinen</span>! "
             "<span begin=\"3.0\" end=\"3.5\">laulu</span>"
             "<span begin=\"3.5\" end=\"3.6\">,</span> "
             "<span begin=\"4.0\" end=\"4.5\">loppu</span>...</p>");
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 1.0, 1.5, "sana,", 0, true },
                { 2.0, 2.5, "toinen!", 0, true },
                { 3.0, 3.6, "laulu,", 0, true },
                { 4.0, 4.5, "loppu...", 0, true },
            });
        if (QTest::currentTestFailed()) return;

        r = parse("<p>\"<span begin=\"1.0\" end=\"1.5\">lainaus</span>\" ja "
                  "<span begin=\"2.0\" end=\"2.5\">muuta</span> "
                  "(sivuhuomio)</p>");
        QCOMPARE(r.error, QString());
        compareWords(r.lyrics, {
                { 1.0, 1.5, "lainaus\"", 0, true },
                { 2.0, 2.5, "muuta", 0, true },
            });
        verifyOneWarning(r, "3 ");
    }

    // A span with no begin is read through, as if it were not there
    void wrappers_read_through() {
        LyricsParseResult r = parse
            ("<p><span><span begin=\"1.0\" end=\"1.5\">yksi</span> "
             "<span begin=\"1.5\" end=\"2.0\">kak</span></span>"
             "<span begin=\"2.0\" end=\"2.5\">si</span> "
             "<span xml:lang=\"fi\"><span><span begin=\"3.0\" end=\"3.5\">"
             "kolme</span></span></span> "
             "<span begin=\"4.0\" end=\"4.5\">neljä</span><span>!</span></p>");
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 1.0, 1.5, "yksi", 0, true },
                { 1.5, 2.5, "kaksi", 0, true },
                { 3.0, 3.5, "kolme", 0, true },
                { 4.0, 4.5, "neljä!", 0, true },
            });
    }

    // Background vocals, translations and romanisations are left out
    // with everything in them, whatever namespace their role is in,
    // with one warning that counts them. One between two words does
    // not join them.
    void skipped_roles_warned_once() {
        LyricsParseResult r = parse
            ("<p><span begin=\"1.0\" end=\"1.5\">yksi</span>"
             "<span ttm:role=\"x-bg\"><span begin=\"1.5\" end=\"2.0\">(tausta</span> "
             "<span begin=\"2.0\" end=\"2.5\">ääni)</span></span>"
             "<span begin=\"2.5\" end=\"3.0\">kaksi</span>"
             "<span ttm:role=\"x-translation\" xml:lang=\"en\">one two</span> "
             "<span begin=\"3.0\" end=\"3.5\">kolme</span>"
             "<span ttm:role=\"x-roman\">kolme</span>"
             "<span xmlns:x=\"urn:x-other\" x:role=\"x-romanization\">k</span> "
             "<span role=\"x-bg\" begin=\"3.5\" end=\"4.0\">tausta</span> "
             "<span ttm:role=\"x-singer x-bg\" begin=\"4.0\" end=\"4.5\">ei</span> "
             "<span ttm:role=\"x-singer\" begin=\"5.0\" end=\"5.5\">laulaja</span>"
             "</p>");
        QCOMPARE(r.error, QString());
        compareWords(r.lyrics, {
                { 1.0, 1.5, "yksi", 0, true },
                { 2.5, 3.0, "kaksi", 0, true },
                { 3.0, 3.5, "kolme", 0, true },
                { 5.0, 5.5, "laulaja", 0, true },
            });
        verifyOneWarning(r, "6 ");
    }

    // A <p> with no timed spans is one word, the whole line, white
    // space made single spaces; with no begin it is left out
    void line_timed_paragraphs() {
        LyricsParseResult r = parse
            ("<p begin=\"1.0\" end=\"3.0\">Koko   rivi\n"
             "   yhdellä  <br/>leimalla</p>\n"
             "<p begin=\"4.0\" end=\"5.0\"><span>Rivi</span> "
             "<span xml:lang=\"fi\">käärittynä</span></p>\n"
             "<p>Ei aikaa</p>\n"
             "<p begin=\"6.0\" end=\"7.0\">   </p>\n"
             "<p begin=\"8.0\" end=\"9.0\">Tekstiä<span ttm:role=\"x-translation\">"
             "Text</span></p>\n");
        QCOMPARE(r.error, QString());
        QVERIFY(!r.lyrics.wordTimed);
        QCOMPARE(r.lyrics.lineCount(), 3);
        compareWords(r.lyrics, {
                { 1.0, 3.0, "Koko rivi yhdellä leimalla", 0, true },
                { 4.0, 5.0, "Rivi käärittynä", 1, true },
                { 8.0, 9.0, "Tekstiä", 2, true },
            });
        QCOMPARE(r.warnings.size(), qsizetype(2));
        QVERIFY2(r.warnings[0].startsWith("1 line had no time"),
                 qPrintable(r.warnings.join(" | ")));
    }

    // A missing end is the next word's start in the line, else the
    // line's end, else inferred as the LRC parser infers it. A line of
    // timed words needs no begin of its own, so one it cannot read
    // takes nothing away.
    void missing_ends_inferred() {
        LyricsParseResult r = parse
            ("<p><span begin=\"1.0\">a</span> <span begin=\"1.5\">b</span></p>"
             "<p begin=\"väärä\" end=\"4.0\"><span begin=\"2.5\">c</span> "
             "<span begin=\"3.0\">d</span></p>"
             "<p><span begin=\"10.0\">e</span></p>"
             "<p begin=\"20.0\">rivi</p>"
             "<p begin=\"30.0\" dur=\"1.5\">kesto</p>"
             "<p begin=\"40.0\">viimeinen rivi</p>");
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        QVERIFY(r.lyrics.wordTimed);
        compareWords(r.lyrics, {
                { 1.0, 1.5, "a", 0, false },
                { 1.5, 2.5, "b", 0, false },     // up to the next line
                { 2.5, 3.0, "c", 1, false },
                { 3.0, 4.0, "d", 1, true },      // the line's end
                { 10.0, 10.0 + W, "e", 2, false },
                { 20.0, 30.0, "rivi", 3, false },
                { 30.0, 31.5, "kesto", 4, true },
                { 40.0, 40.0 + L, "viimeinen rivi", 5, false },
            });
        if (QTest::currentTestFailed()) return;

        r = parse("<p><span begin=\"1.0\">a</span></p>");
        compareWords(r.lyrics, { { 1.0, 1.0 + W, "a", 0, false } });
    }

    // An end before its start is made the start, with a warning
    void end_before_start_becomes_start() {
        LyricsParseResult r = parse
            ("<p><span begin=\"2.0\" end=\"1.0\">a</span> "
             "<span begin=\"3.0\" end=\"3.5\">b</span></p>"
             "<p begin=\"5.0\" end=\"4.0\">rivi</p>");
        QCOMPARE(r.error, QString());
        compareWords(r.lyrics, {
                { 2.0, 2.0, "a", 0, true },
                { 3.0, 3.5, "b", 0, true },
                { 5.0, 5.0, "rivi", 1, true },
            });
        verifyOneWarning(r, "2 ");
    }

    // Entities are decoded, then the text is cleaned as the LRC
    // parser cleans it; blanks inside a word stay, as in LRC
    void text_cleaned() {
        QByteArray div =
            "<p><span begin=\"1.0\" end=\"1.5\">rock &amp; roll</span> "
            "<span begin=\"2.0\" end=\"2.5\">&lt;3</span> "
            "<span begin=\"3.0\" end=\"3.5\">&#xE4;iti&#228;</span> "
            "<span begin=\"4.0\" end=\"4.5\">&quot;&apos;&gt;</span> "
            "<span begin=\"5.0\" end=\"5.5\"><![CDATA[a<b&c]]></span> "
            "<span begin=\"6.0\" end=\"6.5\">a&#x7F;b\x7F" "c</span> "
            "<span begin=\"7.0\" end=\"7.5\">x&#9;y&#10;z&#13;w</span> "
            "<span begin=\"8.0\" end=\"8.5\">kaksi  sanaa</span> "
            "<span begin=\"9.0\" end=\"9.5\">" + QByteArray(1000, 'x') +
            "</span></p>";
        LyricsParseResult r = parseTtml(ttml(div.constData()));
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 1.0, 1.5, "rock & roll", 0, true },
                { 2.0, 2.5, "<3", 0, true },
                { 3.0, 3.5, "äitiä", 0, true },
                { 4.0, 4.5, "\"'>", 0, true },
                { 5.0, 5.5, "a<b&c", 0, true },
                { 6.0, 6.5, "abc", 0, true },
                { 7.0, 7.5, "x y z w", 0, true },
                { 8.0, 8.5, "kaksi  sanaa", 0, true },
                { 9.0, 9.5, QByteArray(Lyrics::maxLabelLength, 'x').constData(),
                  0, true },
            });
        if (QTest::currentTestFailed()) return;

        // never cut through the middle of a character that needs two
        const char32_t clef[] = { 0x1D11E };
        QString text = QString(Lyrics::maxLabelLength - 1, 'z') +
            QString::fromUcs4(clef, 1) + "zz";
        r = parseTtml(ttml(("<p begin=\"1.0\" end=\"2.0\">" + text +
                            "</p>").toUtf8().constData()));
        QCOMPARE(wordCount(r), 1);
        QCOMPARE(r.lyrics.words[0].text,
                 QString(Lyrics::maxLabelLength - 1, 'z'));
    }

    // <ttm:title> in the head's metadata is the title: the first one,
    // and none from a <p>'s metadata, which is not lyrics either
    void title_from_head_metadata() {
        LyricsParseResult r = parseTtml
            (ttml("<p begin=\"1.0\" end=\"2.0\"><metadata><ttm:title>Ei"
                  "</ttm:title></metadata>sana</p>",
                  "<ttm:agent type=\"person\" xml:id=\"v1\"/>"
                  "<ttm:title>Kesäyön  &amp;\n testi</ttm:title>"
                  "<ttm:title>Toinen</ttm:title>"));
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        QCOMPARE(r.lyrics.title, QString::fromUtf8("Kesäyön & testi"));
        QCOMPARE(r.lyrics.artist, QString());
        compareWords(r.lyrics, { { 1.0, 2.0, "sana", 0, true } });

        r = parse("<p begin=\"1.0\" end=\"2.0\">sana</p>");
        QCOMPARE(r.lyrics.title, QString());
    }

    // Nothing usable is an error, and gives no lyrics
    void no_words_is_an_error() {
        const QByteArray files[] = {
            ttml(""),
            ttml("<p>ilman aikaa</p>", "<ttm:title>Nimi</ttm:title>"),
            ttml("<p begin=\"1.0\" end=\"2.0\"> </p>"),
            ttml("<p><span ttm:role=\"x-bg\"><span begin=\"1.0\" end=\"2.0\">"
                 "tausta</span></span></p>"),
            ttml("<p><span begin=\"x\" end=\"2.0\">sana</span></p>"),
            QByteArray("<html><body><div>Hei</div></body></html>"),
            QByteArray("<tt/>"),
            QByteArray(),
        };
        for (const QByteArray &file : files) {
            LyricsParseResult r = parseTtml(file);
            QVERIFY2(!r.error.isEmpty(), file.constData());
            QVERIFY(r.lyrics.isEmpty());
            QCOMPARE(r.lyrics.title, QString());
        }
    }

    // Malformed XML is an error that names the line, even when words
    // were read before it
    void malformed_xml_names_the_line() {
        LyricsParseResult r = parseTtml
            ("<tt>\n<body>\n<div>\n"
             "<p begin=\"1.0\"><span begin=\"1.0\">a</p>\n"
             "</div>\n</body>\n</tt>\n");
        QVERIFY(r.lyrics.isEmpty());
        QVERIFY2(r.error.contains("line 4"), qPrintable(r.error));

        QByteArray whole = fixture("moises-exporter-words.ttml");
        QVERIFY(!whole.isEmpty());
        r = parseTtml(whole.left(whole.indexOf("itunes:key=\"L3\"")));
        QVERIFY(r.lyrics.isEmpty());
        QVERIFY2(r.error.contains("line 12"), qPrintable(r.error));

        // An entity XML itself does not have
        r = parse("<p begin=\"1.0\" end=\"2.0\">a&nbsp;b</p>");
        QVERIFY(r.lyrics.isEmpty());
        QVERIFY2(r.error.contains("line 2"), qPrintable(r.error));

        // Bytes that are not the UTF-8 it says it is. The reader
        // decodes ahead of where it reads, so the line it names is not
        // the one they are on.
        r = parseTtml("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<tt><body><div><p begin=\"1.0\" end=\"2.0\">"
                      "H\xE4m\xE4r\xE4</p></div></body></tt>");
        QVERIFY(r.lyrics.isEmpty());
        QVERIFY2(r.error.contains("line "), qPrintable(r.error));
    }

    // A file with a DTD is refused before anything in it is read: it
    // is how entity tricks get in, and TTML has none
    void dtd_refused() {
        const char *files[] = {
            "<?xml version=\"1.0\"?>\n"
            "<!DOCTYPE tt [ <!ENTITY a \"aaaaaaaaaa\">\n"
            "<!ENTITY b \"&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;\"> ]>\n"
            "<tt><body><div><p begin=\"1.0\" end=\"2.0\">&b;</p>"
            "</div></body></tt>\n",

            "<!DOCTYPE tt>\n"
            "<tt><body><div><p begin=\"1.0\" end=\"2.0\">sana</p>"
            "</div></body></tt>\n",

            "<!DOCTYPE tt SYSTEM \"file:///etc/passwd\">\n"
            "<tt><body><div><p begin=\"1.0\" end=\"2.0\">sana</p>"
            "</div></body></tt>\n",
        };
        for (const char *file : files) {
            LyricsParseResult r = parseTtml(file);
            QVERIFY2(r.error.contains("DOCTYPE"), qPrintable(r.error));
            QVERIFY2(r.lyrics.isEmpty(), qPrintable(describe(r.lyrics)));
        }
    }

    // Exactly the limit is read; one byte more is not
    void too_big_refused() {
        QByteArray big = ttml("<p begin=\"1.0\" end=\"2.0\">sana</p>");
        big += QByteArray(Lyrics::maxFileBytes - big.size(), '\n');
        LyricsParseResult r = parseTtml(big);
        QCOMPARE(r.error, QString());
        QCOMPARE(wordCount(r), 1);

        big += '\n';
        r = parseTtml(big);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.lyrics.isEmpty());
        QCOMPARE(parseLyrics(big).error, r.error);
    }

    // Lines in time order by their first start, those that start
    // together in file order; the words of a line in time order
    void lines_sorted_by_first_start() {
        LyricsParseResult r = parse
            ("<p><span begin=\"20.0\" end=\"21.0\">kolmas</span></p>"
             "<p><span begin=\"5.5\" end=\"6.0\">toinen</span> "
             "<span begin=\"5.0\" end=\"5.5\">ensin</span></p>"
             "<p begin=\"0.0\" end=\"1.0\">eka</p>"
             "<p begin=\"0.0\" end=\"0.5\">toka</p>");
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 0.0, 1.0, "eka", 0, true },
                { 0.0, 0.5, "toka", 1, true },
                { 5.0, 5.5, "ensin", 2, true },
                { 5.5, 6.0, "toinen", 2, true },
                { 20.0, 21.0, "kolmas", 3, true },
            });
    }

    // Files without the TTML namespace, with a prefix for it, and
    // with the time attributes in another namespace
    void names_matched_by_local_name() {
        const char *files[] = {
            "<tt><body><div><p><span begin=\"1.0\" end=\"2.0\">sana</span>"
            "</p></div></body></tt>",

            "<tt:tt xmlns:tt=\"http://www.w3.org/ns/ttml\"><tt:body><tt:div>"
            "<tt:p><tt:span begin=\"1.0\" end=\"2.0\">sana</tt:span></tt:p>"
            "</tt:div></tt:body></tt:tt>",

            "<tt xmlns=\"http://www.w3.org/ns/ttml\" xmlns:x=\"urn:x\">"
            "<body><div><p><span x:begin=\"1.0\" x:end=\"2.0\">sana</span>"
            "</p></div></body></tt>",
        };
        for (const char *file : files) {
            LyricsParseResult r = parseTtml(file);
            QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
            QCOMPARE(r.warnings, QStringList());
            compareWords(r.lyrics, { { 1.0, 2.0, "sana", 0, true } });
            if (QTest::currentTestFailed()) return;
        }
    }

    // What the exporter's own TTML code writes in word mode, from an
    // invented lyrics.json whose words were, in seconds:
    //   Tämä 0.52-0.8, on 0.84-1.02, keksitty 1.1-1.9 (syllables kek
    //   1.1-1.35, sit 1.35-1.62, ty 1.62-1.9), laulu 2.0-2.7, ","
    //   2.7-2.75 | Yö 4.64-4.9, on 4.92-5.1, "hämärä," 5.3-6.2, kuu
    //   6.25-6.8, "nousee." 6.8-7.5 | Nyt 61.25-61.6, se 61.7-61.9,
    //   "loppuu!" 62.0-63.5
    // The exporter glues a word that starts with punctuation to the
    // one before it, so the comma joins "laulu" as a syllable would.
    void exporter_word_fixture() {
        QByteArray bytes = fixture("moises-exporter-words.ttml");
        QVERIFY(!bytes.isEmpty());
        LyricsParseResult r = parseTtml(bytes);
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        QCOMPARE(r.lyrics.title, QString());
        QVERIFY(r.lyrics.wordTimed);
        QCOMPARE(r.lyrics.lineCount(), 3);
        compareWords(r.lyrics, {
                { 0.52, 0.8, "Tämä", 0, true },
                { 0.84, 1.02, "on", 0, true },
                { 1.1, 1.9, "keksitty", 0, true },
                { 2.0, 2.75, "laulu,", 0, true },
                { 4.64, 4.9, "Yö", 1, true },
                { 4.92, 5.1, "on", 1, true },
                { 5.3, 6.2, "hämärä,", 1, true },
                { 6.25, 6.8, "kuu", 1, true },
                { 6.8, 7.5, "nousee.", 1, true },
                { 61.25, 61.6, "Nyt", 2, true },
                { 61.7, 61.9, "se", 2, true },
                { 62.0, 63.5, "loppuu!", 2, true },
            });
    }

    // The same in line mode: each line one word, from its first
    // word's start to its last word's end
    void exporter_line_fixture() {
        QByteArray bytes = fixture("moises-exporter-lines.ttml");
        QVERIFY(!bytes.isEmpty());
        LyricsParseResult r = parseTtml(bytes);
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        QVERIFY(!r.lyrics.wordTimed);
        QCOMPARE(r.lyrics.lineCount(), 3);
        compareWords(r.lyrics, {
                { 0.52, 2.75, "Tämä on keksitty laulu,", 0, true },
                { 4.64, 7.5, "Yö on hämärä, kuu nousee.", 1, true },
                { 61.25, 63.5, "Nyt se loppuu!", 2, true },
            });
    }

    // As AMLL TTML Tool writes it: mm:ss.mmm, two agents, syllables,
    // a background vocal and translations, which are skipped
    void amll_fixture() {
        QByteArray bytes = fixture("amll-style.ttml");
        QVERIFY(!bytes.isEmpty());
        LyricsParseResult r = parseTtml(bytes);
        QCOMPARE(r.error, QString());
        verifyOneWarning(r, "3 ");
        QCOMPARE(r.lyrics.title, QString());
        QVERIFY(r.lyrics.wordTimed);
        QCOMPARE(r.lyrics.lineCount(), 3);
        compareWords(r.lyrics, {
                { 3.2, 3.55, "Pöllö", 0, true },
                { 3.6, 4.05, "huhuilee", 0, true },
                { 4.3, 4.6, "ja", 0, true },
                { 4.6, 5.1, "kuu", 0, true },
                { 7.0, 7.65, "nousee", 1, true },
                { 7.8, 8.5, "metsän", 1, true },
                { 8.6, 9.4, "ylle", 1, true },
                { 65.0, 65.8, "Hiljaa", 2, true },
                { 65.9, 69.5, "hiljaa", 2, true },
            });
    }

    // TTML if the first character that is not blank, after a BOM, is
    // '<'; LRC otherwise
    void parse_lyrics_chooses_the_format() {
        const QByteArray bom("\xEF\xBB\xBF");
        const QByteArray lrc = fixture("moises-exporter-words.lrc");
        const QByteArray tt = fixture("moises-exporter-words.ttml");
        QVERIFY(!lrc.isEmpty() && !tt.isEmpty());

        const Lyrics fromLrc = parseLrc(lrc).lyrics;
        const Lyrics fromTtml = parseTtml(tt).lyrics;
        QVERIFY(!fromLrc.isEmpty() && !fromTtml.isEmpty());

        auto same = [](const Lyrics &a, const Lyrics &b) {
            if (a.words.size() != b.words.size()) return false;
            for (int i = 0; i < a.words.size(); ++i) {
                if (a.words[i].text != b.words[i].text ||
                    a.words[i].start != b.words[i].start ||
                    a.words[i].end != b.words[i].end) return false;
            }
            return true;
        };

        QVERIFY(same(parseLyrics(lrc).lyrics, fromLrc));
        QVERIFY(same(parseLyrics(bom + lrc).lyrics, fromLrc));
        QVERIFY(same(parseLyrics(" \r\n\t" + lrc).lyrics, fromLrc));
        QVERIFY(same(parseLyrics(tt).lyrics, fromTtml));
        QVERIFY(same(parseLyrics(bom + tt).lyrics, fromTtml));

        // Blanks before the root are XML too, when there is no
        // declaration, which has to come first
        QByteArray noDeclaration = tt.mid(tt.indexOf("<tt"));
        QVERIFY(same(parseLyrics("\n \t\r\n" + noDeclaration).lyrics,
                     fromTtml));
        QVERIFY(same(parseLyrics(bom + "\n  " + noDeclaration).lyrics,
                     fromTtml));

        // UTF-16, which the XML reader decodes itself
        const QStringConverter::Encoding encodings[] = {
            QStringConverter::Utf16LE, QStringConverter::Utf16BE
        };
        for (QStringConverter::Encoding e : encodings) {
            QStringEncoder encoder(e, QStringConverter::Flag::WriteBom);
            QByteArray bytes = encoder.encode
                ("\n" + QString::fromUtf8(noDeclaration));
            LyricsParseResult r = parseLyrics(bytes);
            QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
            QVERIFY(same(r.lyrics, fromTtml));
        }

        // What each parser says of a file it cannot use
        QCOMPARE(parseLyrics("<tt/>").error, parseTtml("<tt/>").error);
        QCOMPARE(parseLyrics("sana").error, parseLrc("sana").error);
        QVERIFY(parseTtml("<tt/>").error != parseLrc("sana").error);
        QVERIFY(!parseLyrics(QByteArray()).error.isEmpty());
    }

    // The writer's layout, as the exporter's: one agent, a <p> per
    // line and a <span> per word with a space between, m:ss.mmm
    void write_format() {
        Lyrics lyrics;
        lyrics.title = "Testi & laulu";
        lyrics.artist = "Ei kirjoiteta";
        auto word = [](double start, double end, const char *text, int line) {
            LyricWord w;
            w.start = start;
            w.end = end;
            w.text = QString::fromUtf8(text);
            w.line = line;
            return w;
        };
        lyrics.words = {
            word(0.5, 1.0, "Yö", 0),
            word(1.0, 1.25, "on", 0),
            word(65.4321, 66.0, "<kaunis>", 1),
            word(66.0, 70.0006, "\"kaksi sanaa\"", 1),
        };
        const QByteArray expected =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<tt xmlns=\"http://www.w3.org/ns/ttml\" "
            "xmlns:itunes=\"http://music.apple.com/lyric-ttml-internal\" "
            "xmlns:ttm=\"http://www.w3.org/ns/ttml#metadata\" "
            "itunes:timing=\"Word\">\n"
            "  <head>\n"
            "    <metadata>\n"
            "      <ttm:title>Testi &amp; laulu</ttm:title>\n"
            "      <ttm:agent type=\"person\" xml:id=\"v1\"/>\n"
            "    </metadata>\n"
            "  </head>\n"
            "  <body dur=\"1:10.001\">\n"
            "    <div begin=\"0:00.500\" end=\"1:10.001\">\n"
            "      <p begin=\"0:00.500\" end=\"0:01.250\" ttm:agent=\"v1\" "
            "itunes:key=\"L1\"><span begin=\"0:00.500\" end=\"0:01.000\">"
            "Yö</span> <span begin=\"0:01.000\" end=\"0:01.250\">on</span>"
            "</p>\n"
            "      <p begin=\"1:05.432\" end=\"1:10.001\" ttm:agent=\"v1\" "
            "itunes:key=\"L2\"><span begin=\"1:05.432\" end=\"1:06.000\">"
            "&lt;kaunis&gt;</span> <span begin=\"1:06.000\" "
            "end=\"1:10.001\">&quot;kaksi sanaa&quot;</span></p>\n"
            "    </div>\n"
            "  </body>\n"
            "</tt>\n";
        const QByteArray written = writeTtml(lyrics);
        QVERIFY2(written == expected,
                 qPrintable(QString("written:\n%1\nexpected:\n%2")
                            .arg(QString::fromUtf8(written))
                            .arg(QString::fromUtf8(expected))));
    }

    // What is written reads back as the same words, texts and lines,
    // to half a millisecond: from LRC, from TTML, and awkward cases
    void round_trip() {
        const char *lrcFixtures[] = {
            "moises-exporter-words.lrc", "moises-exporter-lines.lrc",
            "lrc-with-ends.lrc",
        };
        for (const char *name : lrcFixtures) {
            LyricsParseResult r = parseLrc(fixture(name));
            QVERIFY2(r.error.isEmpty(), name);
            verifyRoundTrip(r.lyrics, name);
            if (QTest::currentTestFailed()) return;
        }
        const char *ttmlFixtures[] = {
            "moises-exporter-words.ttml", "moises-exporter-lines.ttml",
            "amll-style.ttml",
        };
        for (const char *name : ttmlFixtures) {
            LyricsParseResult r = parseTtml(fixture(name));
            QVERIFY2(r.error.isEmpty(), name);
            verifyRoundTrip(r.lyrics, name);
            if (QTest::currentTestFailed()) return;
        }

        // Markup characters, blanks inside a word, a character that
        // needs two, punctuation as a word of its own, words of no
        // length and at the same time, times off the millisecond and
        // past an hour
        Lyrics lyrics;
        lyrics.title = QString::fromUtf8("Hämärä <yö> & \"päivä\"");
        auto word = [](double start, double end, const QString &text,
                       int line) {
            LyricWord w;
            w.start = start;
            w.end = end;
            w.text = text;
            w.line = line;
            return w;
        };
        const char32_t clef[] = { 0x1D11E };
        lyrics.words = {
            word(0.0, 0.0, "a&b", 0),
            word(0.0, 1.2344, "<3", 0),
            word(1.2346, 2.0, "koko  rivi \"lainaus\" 'x'", 0),
            word(2.0, 2.0, ",", 0),
            word(2.5, 3.0, QString::fromUtf8("äiti ") +
                 QString::fromUcs4(clef, 1), 1),
            word(2.9, 3.5, "]]>", 1),
            word(3725.4996, 3726.0004, "tunti", 2),
            word(3726.0004, 3726.0004, "yli", 2),
        };
        verifyRoundTrip(lyrics, "awkward");
    }
};

#endif
