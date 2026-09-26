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

#ifndef TEST_LYRICS_H
#define TEST_LYRICS_H

// Tier 2: reading LRC files, and the events the lyrics become. No
// window and no model: what the application does with them is Tier
// 5's business (TestRecordWorkflow). The files in testdata/lyrics are
// written as Moises-Lyric-Exporter writes them, with invented text.

#include "../Lyrics.h"

#include "base/EventSeries.h"
#include "base/XmlExportable.h"

#include <QObject>
#include <QtTest>
#include <QFile>
#include <QStringConverter>
#include <QXmlStreamReader>

#include <cmath>

class TestLyrics : public QObject
{
    Q_OBJECT

    // The inferred lengths, as the parser has them
    static constexpr double W = Lyrics::inferredWordSeconds;
    static constexpr double L = Lyrics::inferredLastLineSeconds;

    static LyricsParseResult parse(const char *text) {
        return parseLrc(QByteArray(text));
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
            .arg(w.text).arg(w.start, 0, 'f', 3).arg(w.end, 0, 'f', 3)
            .arg(w.line).arg(w.endGiven ? ", end given" : "");
    }

    static QString describe(const Lyrics &lyrics) {
        QStringList list;
        for (const LyricWord &w : lyrics.words) list << describe(w);
        return list.join("; ");
    }

    // Every LRC time is whole milliseconds, and so is every expected
    // time here: this only absorbs the last bit of a double
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

private slots:
    // Line timing: a line lasts until the next one starts, however far
    // away that is, and the last one for a default length
    void line_lrc_starts_and_ends() {
        LyricsParseResult r = parse("[00:01.00]Ensimmäinen rivi\n"
                                    "[00:04.50]Toinen rivi\n"
                                    "[00:30.00]Kolmas rivi\n");
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        QVERIFY(!r.lyrics.wordTimed);
        QCOMPARE(r.lyrics.lineCount(), 3);
        compareWords(r.lyrics, {
                { 1.0, 4.5, "Ensimmäinen rivi", 0, false },
                { 4.5, 30.0, "Toinen rivi", 1, false },
                { 30.0, 30.0 + L, "Kolmas rivi", 2, false },
            });
    }

    // One fraction digit is tenths, two hundredths, three thousandths,
    // and ':' does as well as '.'
    void time_tag_precision() {
        const char *tags[] = {
            "[01:02.5]", "[01:02.50]", "[01:02.500]", "[01:02:50]",
            "[01:02:5]", "[01:02:500]"
        };
        for (const char *tag : tags) {
            LyricsParseResult r = parseLrc(QByteArray(tag) + "sana\n");
            QVERIFY2(r.error.isEmpty(), tag);
            QCOMPARE(wordCount(r), 1);
            QVERIFY2(r.lyrics.words[0].start == 62.5, tag);
        }

        LyricsParseResult r = parse("[01:02]sana\n");
        QCOMPARE(wordCount(r), 1);
        QCOMPARE(r.lyrics.words[0].start, 62.0);

        r = parse("[00:01.234]a\n[00:01.3]b\n");
        QCOMPARE(wordCount(r), 2);
        QCOMPARE(r.lyrics.words[0].start, 1.234);
        QCOMPARE(r.lyrics.words[1].start, 1.3);

        // Word tags are read the same way
        r = parse("[01:00.00]<01:02.5>a <01:03:25>b <01:04.125>c\n");
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 62.5, 63.25, "a", 0, true },
                { 63.25, 64.125, "b", 0, true },
                { 64.125, 64.125 + W, "c", 0, false },
            });

        // Four fraction digits make no tag
        r = parse("[00:01.2345]ei\n[00:02.00]kyllä\n");
        QCOMPARE(wordCount(r), 1);
        QCOMPARE(r.lyrics.words[0].text, QString("kyllä"));
        QCOMPARE(r.warnings.size(), qsizetype(1));
    }

    // Songs over an hour: minutes have as many digits as they need
    void minutes_over_59() {
        LyricsParseResult r = parse("[75:00.00]pitkä\n"
                                    "[123:04.5]<123:04.5>hyvin <123:05.25>pitkä\n");
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 4500.0, 7384.5, "pitkä", 0, false },
                { 7384.5, 7385.25, "hyvin", 1, true },
                { 7385.25, 7385.25 + W, "pitkä", 1, false },
            });

        // but not so many that the arithmetic overflows: that is no
        // time tag, and the line is skipped
        r = parse("[00:01.00]sana\n[99999999999999999999:00.00]ei\n");
        QCOMPARE(wordCount(r), 1);
        QCOMPARE(r.warnings.size(), qsizetype(1));
    }

    // Seconds of 60 or more make no time tag: the line is skipped
    void seconds_of_60_or_more_skip_the_line() {
        LyricsParseResult r = parse("[00:60.00]ei\n"
                                    "[00:01.00]kyllä\n"
                                    "[01:75.50]ei tämäkään\n");
        QCOMPARE(r.error, QString());
        compareWords(r.lyrics, { { 1.0, 1.0 + L, "kyllä", 0, false } });
        QCOMPARE(r.warnings.size(), qsizetype(1));
        QVERIFY2(r.warnings[0].startsWith("2 "), qPrintable(r.warnings[0]));

        // Inside a line such a word tag is just text
        r = parse("[00:01.00]<00:01.00>a <00:61.00>b\n");
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, { { 1.0, 1.0 + W, "a <00:61.00>b", 0, false } });
    }

    // [00:12.00][01:30.00]chorus puts the line in at both times, each
    // sorted into place
    void several_leading_time_tags_repeat_the_line() {
        LyricsParseResult r = parse("[00:30.00]säkeistö\n"
                                    "[00:12.00][01:30.00]kertosäe\n"
                                    "[01:00.00]väliosa\n");
        QCOMPARE(r.warnings, QStringList());
        QCOMPARE(r.lyrics.lineCount(), 4);
        compareWords(r.lyrics, {
                { 12.0, 30.0, "kertosäe", 0, false },
                { 30.0, 60.0, "säkeistö", 1, false },
                { 60.0, 90.0, "väliosa", 2, false },
                { 90.0, 90.0 + L, "kertosäe", 3, false },
            });

        // Word tags move with the stamp the line is repeated at
        r = parse("[00:10.00][00:20.00]<00:10.00>la <00:10.50>lu\n");
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 10.0, 10.5, "la", 0, true },
                { 10.5, 10.5 + W, "lu", 0, false },
                { 20.0, 20.5, "la", 1, true },
                { 20.5, 20.5 + W, "lu", 1, false },
            });
    }

    // Only the title and the artist are kept; other metadata, whatever
    // the case of its key, is neither lyrics nor a problem
    void metadata_ignored_title_and_artist_kept() {
        LyricsParseResult r = parse("[ti: Kesäyö ]\n"
                                    "[AR:Testiryhmä]\n"
                                    "[al:Levy]\n"
                                    "[by:Joku]\n"
                                    "[au:Tekijä]\n"
                                    "[length: 03:25]\n"
                                    "[#:kommentti]\n"
                                    "[00:01.00]sana\n");
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        QCOMPARE(r.lyrics.title, QString("Kesäyö"));
        QCOMPARE(r.lyrics.artist, QString("Testiryhmä"));
        compareWords(r.lyrics, { { 1.0, 1.0 + L, "sana", 0, false } });
    }

    // A positive [offset:] makes the lyrics appear sooner, as LRC has
    // it; a negative one later
    void offset_sign() {
        LyricsParseResult r = parse("[offset:+500]\n"
                                    "[00:01.00]yksi\n"
                                    "[00:03.00]<00:03.00>kaksi <00:03.40>kolme\n");
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 0.5, 2.5, "yksi", 0, false },
                { 2.5, 2.9, "kaksi", 1, true },
                { 2.9, 2.9 + W, "kolme", 1, false },
            });

        r = parse("[offset:-500]\n[00:01.00]yksi\n");
        compareWords(r.lyrics, { { 1.5, 1.5 + L, "yksi", 0, false } });

        // Wherever it is in the file, it counts for all of it
        r = parse("[00:01.00]yksi\n[offset:250]\n");
        compareWords(r.lyrics, { { 0.75, 0.75 + L, "yksi", 0, false } });

        // A word it pushes below 0 is dropped, and the lines are
        // numbered without it
        r = parse("[offset:+1500]\n[00:01.00]yksi\n[00:03.00]kaksi\n");
        compareWords(r.lyrics, { { 1.5, 1.5 + L, "kaksi", 0, false } });
        QCOMPARE(r.warnings.size(), qsizetype(1));

        // Exactly 0 is not below it
        r = parse("[offset:1000]\n[00:01.00]yksi\n");
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, { { 0.0, L, "yksi", 0, false } });

        // Not a number: no offset, and a warning
        r = parse("[offset:puoli sekuntia]\n[00:01.00]yksi\n");
        compareWords(r.lyrics, { { 1.0, 1.0 + L, "yksi", 0, false } });
        QCOMPARE(r.warnings.size(), qsizetype(1));
    }

    // Word timing: a word ends where the next word tag is, or where a
    // trailing tag says; the last word of a line with neither ends at
    // the next line, or a default length after its start
    void word_lrc_starts_and_ends() {
        LyricsParseResult r = parse
            ("[00:01.00]<00:01.00>Päivä <00:01.50>paistaa <00:02.20>ja<00:02.60>\n"
             "[00:05.00]<00:05.00>kuu <00:05.40>nousee\n"
             "[00:06.00]<00:06.00>yö\n");
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        QVERIFY(r.lyrics.wordTimed);
        QCOMPARE(r.lyrics.lineCount(), 3);
        compareWords(r.lyrics, {
                { 1.0, 1.5, "Päivä", 0, true },
                { 1.5, 2.2, "paistaa", 0, true },
                { 2.2, 2.6, "ja", 0, true },
                { 5.0, 5.4, "kuu", 1, true },
                { 5.4, 6.0, "nousee", 1, false },
                { 6.0, 6.0 + W, "yö", 2, false },
            });

        // Text before the first word tag starts at the line's own time
        r = parse("[00:10.00] Alku <00:10.50>loppu\n");
        compareWords(r.lyrics, {
                { 10.0, 10.5, "Alku", 0, true },
                { 10.5, 10.5 + W, "loppu", 0, false },
            });
    }

    // A timed line with no text, or only notes, ends the line before
    // it and is no line itself. With neither, the last word of a line
    // lasts until the next line, but no longer than the cap.
    void empty_or_music_line_ends_the_line_before() {
        LyricsParseResult r = parse("[00:01.00]<00:01.00>yksi <00:01.50>kaksi\n"
                                    "[00:04.00]\n"
                                    "[00:10.00]<00:10.00>kolme\n"
                                    "[00:12.50] ♪\n"
                                    "[00:15.00]<00:15.00>neljä\n"
                                    "[00:15.80] ♫ ♪ \n"
                                    "[00:20.00]<00:20.00>viisi <00:20.40>kuusi\n"
                                    "[00:30.00]<00:30.00>seitsemän\n");
        QCOMPARE(r.warnings, QStringList());
        QCOMPARE(r.lyrics.lineCount(), 5);
        compareWords(r.lyrics, {
                { 1.0, 1.5, "yksi", 0, true },
                { 1.5, 4.0, "kaksi", 0, true },
                { 10.0, 12.5, "kolme", 1, true },
                { 15.0, 15.8, "neljä", 2, true },
                { 20.0, 20.4, "viisi", 3, true },
                { 20.4, 20.4 + W, "kuusi", 3, false },
                { 30.0, 30.0 + W, "seitsemän", 4, false },
            });

        // The same for line timing, where nothing but such a line caps
        // how long a line lasts
        r = parse("[00:01.00]rivi yksi\n"
                  "[00:03.00]\n"
                  "[00:10.00]rivi kaksi\n"
                  "[00:12.00]♫\n"
                  "[00:20.00]rivi kolme\n"
                  "[00:40.00]rivi neljä\n");
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 1.0, 3.0, "rivi yksi", 0, true },
                { 10.0, 12.0, "rivi kaksi", 1, true },
                { 20.0, 40.0, "rivi kolme", 2, false },
                { 40.0, 40.0 + L, "rivi neljä", 3, false },
            });
    }

    // The exporter leaves out the space before a word with punctuation
    // in it: words are split on the tags, never on spaces, and the
    // spaces around them are not part of them
    void glued_words_split_on_tags() {
        LyricsParseResult r = parse
            ("[00:04.640] <00:04.640>Tämä <00:04.920>on <00:05.160>keksitty<00:05.620>laulu,\n"
             "[00:07.000]   <00:07.000>   väli   <00:07.500>  lopussa  \n");
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 4.64, 4.92, "Tämä", 0, true },
                { 4.92, 5.16, "on", 0, true },
                { 5.16, 5.62, "keksitty", 0, true },
                { 5.62, 7.0, "laulu,", 0, false },
                { 7.0, 7.5, "väli", 1, true },
                { 7.5, 7.5 + W, "lopussa", 1, false },
            });

        // Nor is one tag's text split at its spaces: it is one label
        r = parse("[00:01.00]<00:01.00>kaksi sanaa<00:02.00>\n");
        compareWords(r.lyrics, { { 1.0, 2.0, "kaksi sanaa", 0, true } });
    }

    // The exporter clamps negative times to 0, so several lines can
    // share that stamp: all of them stay, in file order
    void lines_stamped_zero_kept_in_file_order() {
        LyricsParseResult r = parse("[00:00.000] <00:00.000>eka\n"
                                    "[00:00.000] <00:00.000>toka\n"
                                    "[00:00.000] <00:00.000>kolmas\n"
                                    "[00:02.500] <00:02.500>neljäs\n");
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 0.0, 0.0, "eka", 0, false },
                { 0.0, 0.0, "toka", 1, false },
                { 0.0, W, "kolmas", 2, false },
                { 2.5, 2.5 + W, "neljäs", 3, false },
            });

        // and all of them become events, one frame long at least
        sv::EventVector events = lyricsToEvents(r.lyrics, 44100);
        QCOMPARE(int(events.size()), 4);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(events[i].getFrame(), sv::sv_frame_t(0));
            QVERIFY(events[i].getDuration() >= 1);
        }

        r = parse("[00:00.00]eka\n[00:00.00]toka\n[00:01.00]kolmas\n");
        compareWords(r.lyrics, {
                { 0.0, 0.0, "eka", 0, false },
                { 0.0, 1.0, "toka", 1, false },
                { 1.0, 1.0 + L, "kolmas", 2, false },
            });
    }

    // A metadata value runs to the last ']'; the exporter's own header
    // lines are no trouble
    void bracketed_title_and_exporter_header() {
        LyricsParseResult r = parse
            ("[ti:Laulu [Live]]\n"
             "[re:Moises Lyrics Exporter Pro]\n"
             "[ve:2.0]\n"
             "[co:avg_confidence=1.000; low_confidence_count=0]\n"
             "\n"
             "[00:01.000] <00:01.000>sana\n");
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        QCOMPARE(r.lyrics.title, QString("Laulu [Live]"));
        QCOMPARE(r.lyrics.artist, QString());
        compareWords(r.lyrics, { { 1.0, 1.0 + W, "sana", 0, false } });
    }

    // A word tag earlier than the word before it is moved up to that
    // word's start, with one warning that counts them
    void backward_word_tags_clamped() {
        LyricsParseResult r = parse
            ("[00:10.00]<00:10.00>a <00:09.50>b <00:10.50>c<00:10.20>\n");
        compareWords(r.lyrics, {
                { 10.0, 10.0, "a", 0, true },
                { 10.0, 10.5, "b", 0, true },
                { 10.5, 10.5, "c", 0, true },
            });
        QCOMPARE(r.warnings.size(), qsizetype(1));
        QVERIFY2(r.warnings[0].startsWith("2 "), qPrintable(r.warnings[0]));
    }

    // Lines are put in time order, and numbered in it; a line's words
    // go with it
    void unsorted_lines_sorted() {
        LyricsParseResult r = parse("[00:20.00]kolmas\n"
                                    "[00:05.00]ensimmäinen\n"
                                    "[00:10.00]toinen\n");
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 5.0, 10.0, "ensimmäinen", 0, false },
                { 10.0, 20.0, "toinen", 1, false },
                { 20.0, 20.0 + L, "kolmas", 2, false },
            });

        r = parse("[00:08.00]<00:08.00>c <00:08.50>d\n"
                  "[00:02.00]<00:02.00>a <00:02.50>b\n");
        compareWords(r.lyrics, {
                { 2.0, 2.5, "a", 0, true },
                { 2.5, 2.5 + W, "b", 0, false },
                { 8.0, 8.5, "c", 1, true },
                { 8.5, 8.5 + W, "d", 1, false },
            });
    }

    // Finnish text survives UTF-8 with and without a BOM, and UTF-16
    // and UTF-32 with one
    void unicode_with_and_without_bom() {
        const QString title = QString::fromUtf8("Hämärä yö");
        const QString line = QString::fromUtf8("Äiti öisin söi jäätelöä");
        const QString text = "[ti:" + title + "]\n[00:01.00]" + line + "\n";

        QVector<QByteArray> files;
        files << text.toUtf8();
        files << QByteArray("\xEF\xBB\xBF") + text.toUtf8();
        const QStringConverter::Encoding encodings[] = {
            QStringConverter::Utf16LE, QStringConverter::Utf16BE,
            QStringConverter::Utf32LE, QStringConverter::Utf32BE
        };
        for (QStringConverter::Encoding e : encodings) {
            QStringEncoder encoder(e, QStringConverter::Flag::WriteBom);
            QByteArray bytes = encoder.encode(text);
            QVERIFY(QStringConverter::encodingForData(bytes) == e);
            files << bytes;
        }

        for (int i = 0; i < files.size(); ++i) {
            LyricsParseResult r = parseLrc(files[i]);
            QVERIFY2(r.error.isEmpty(), qPrintable(QString::number(i)));
            QVERIFY2(r.warnings.isEmpty(),
                     qPrintable(QString("%1: %2").arg(i)
                                .arg(r.warnings.join(" "))));
            QCOMPARE(r.lyrics.title, title);
            QCOMPARE(wordCount(r), 1);
            QCOMPARE(r.lyrics.words[0].text, line);
        }
    }

    // Bytes that are not UTF-8 are read as Latin-1, with a warning, so
    // that the ä and ö of an older file survive
    void non_utf8_read_as_latin1() {
        LyricsParseResult r = parse("[ti:H\xE4m\xE4r\xE4 y\xF6]\n"
                                    "[00:01.00]\xC4iti \xF6isin\n");
        QCOMPARE(r.error, QString());
        QCOMPARE(r.lyrics.title, QString::fromUtf8("Hämärä yö"));
        QCOMPARE(wordCount(r), 1);
        QCOMPARE(r.lyrics.words[0].text, QString::fromUtf8("Äiti öisin"));
        QCOMPARE(r.warnings.size(), qsizetype(1));

        // Even when the only such byte is the file's last, where a
        // decoder that waits for more would say nothing
        r = parse("[00:01.00]Hyv\xE4");
        QCOMPARE(wordCount(r), 1);
        QCOMPARE(r.lyrics.words[0].text, QString::fromUtf8("Hyvä"));
        QCOMPARE(r.warnings.size(), qsizetype(1));

        // With a BOM the file says it is UTF-8: what cannot be read is
        // replaced, with a warning
        r = parse("\xEF\xBB\xBF[00:01.00]a\xFF" "b\n");
        QCOMPARE(wordCount(r), 1);
        QCOMPARE(r.lyrics.words[0].text, QString("a") + QChar(0xFFFD) + "b");
        QCOMPARE(r.warnings.size(), qsizetype(1));
    }

    // CRLF, LF and CR, with or without a newline at the end
    void line_endings() {
        const char *files[] = {
            "[00:01.00]yksi\r\n[00:02.00]kaksi\r\n[00:03.00]kolme",
            "[00:01.00]yksi\r\n[00:02.00]kaksi\r\n[00:03.00]kolme\r\n",
            "[00:01.00]yksi\r[00:02.00]kaksi\r[00:03.00]kolme\r",
            "[00:01.00]yksi\n[00:02.00]kaksi\n[00:03.00]kolme",
        };
        for (const char *file : files) {
            LyricsParseResult r = parse(file);
            QCOMPARE(r.warnings, QStringList());
            compareWords(r.lyrics, {
                    { 1.0, 2.0, "yksi", 0, false },
                    { 2.0, 3.0, "kaksi", 1, false },
                    { 3.0, 3.0 + L, "kolme", 2, false },
                });
            if (QTest::currentTestFailed()) return;
        }
    }

    // Control characters go, as the session file (XML 1.0) could not
    // be read back with one in a label; a tab becomes a space, as a
    // session file would give it back. A long word is cut.
    void control_characters_stripped_and_long_words_cut() {
        QByteArray bytes("[ti:Ni\x01mi]\n[00:01.00]a");
        bytes += '\0';
        bytes += "b\x1F" "c\x7F" "d\te";
        bytes += "\xEF\xBF\xBF" "f\xEF\xBF\xBE" "g\n";      // U+FFFF, U+FFFE
        bytes += "[00:02.00]<00:02.00>h\x02i <00:02.50>\x03 <00:03.00>j\n";
        LyricsParseResult r = parseLrc(bytes);
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());

        // First what it is for: the labels and the title, as a session
        // holds them, are well-formed XML
        QString xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<dataset name=\"" +
            sv::XmlExportable::encodeEntities(r.lyrics.title) + "\">\n";
        for (const sv::Event &e : lyricsToEvents(r.lyrics, 44100)) {
            xml += e.toXmlString("  ");
        }
        xml += "</dataset>\n";
        QXmlStreamReader reader(xml.toUtf8());
        int points = 0;
        while (!reader.atEnd()) {
            if (reader.readNext() == QXmlStreamReader::StartElement &&
                reader.name() == QLatin1String("point")) {
                ++points;
            }
        }
        QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));
        QCOMPARE(points, 3);

        QCOMPARE(r.lyrics.title, QString("Nimi"));
        compareWords(r.lyrics, {
                { 1.0, 2.0, "abcd efg", 0, false },
                { 2.0, 2.5, "hi", 1, true },
                { 3.0, 3.0 + W, "j", 1, false },
            });
        if (QTest::currentTestFailed()) return;

        // A word, or a line, of 1000 characters is cut to the limit
        r = parseLrc("[00:01.00]<00:01.00>" + QByteArray(1000, 'x') +
                     " <00:02.00>lyhyt\n[00:03.00]" + QByteArray(1000, 'y'));
        QCOMPARE(wordCount(r), 3);
        QCOMPARE(r.lyrics.words[0].text, QString(Lyrics::maxLabelLength, 'x'));
        QCOMPARE(r.lyrics.words[1].text, QString("lyhyt"));
        QCOMPARE(r.lyrics.words[2].text, QString(Lyrics::maxLabelLength, 'y'));

        // never through the middle of a character that needs two
        const char32_t clef[] = { 0x1D11E };
        QString text = QString(Lyrics::maxLabelLength - 1, 'z') +
            QString::fromUcs4(clef, 1) + "zz";
        r = parseLrc(("[00:01.00]" + text).toUtf8());
        QCOMPARE(wordCount(r), 1);
        QCOMPARE(r.lyrics.words[0].text,
                 QString(Lyrics::maxLabelLength - 1, 'z'));
    }

    // Brackets and ampersands that are not part of a tag are text
    void markup_characters_kept() {
        LyricsParseResult r = parse
            ("[00:01.00]rock & roll <yeah> [x] a<b c>d <00:6> <1:02.00\n"
             "[00:03.00]<00:03.00>a&b <00:03.50><3 <00:04.00>[x]<00:04.50>x>y <00:05.00>&amp;\n");
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 1.0, 3.0, "rock & roll <yeah> [x] a<b c>d <00:6> <1:02.00", 0, false },
                { 3.0, 3.5, "a&b", 1, true },
                { 3.5, 4.0, "<3", 1, true },
                { 4.0, 4.5, "[x]", 1, true },
                { 4.5, 5.0, "x>y", 1, true },
                { 5.0, 5.0 + W, "&amp;", 1, false },
            });
    }

    // Nothing usable is an error, and gives no lyrics
    void unusable_files_are_errors() {
        LyricsParseResult r = parse("Tämä on vain tekstiä\nilman aikoja\n");
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.lyrics.isEmpty());

        // Timed lines, but none with words
        r = parse("[ti:Nimi]\n[00:01.00]\n[00:02.00] ♪\n");
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.lyrics.isEmpty());
        QCOMPARE(r.lyrics.title, QString());

        // Not text at all
        QByteArray binary;
        for (int i = 0; i < 4096; ++i) binary += char((i * 37) % 256);
        r = parseLrc(binary);
        QVERIFY(!r.error.isEmpty());

        r = parseLrc(QByteArray());
        QVERIFY(!r.error.isEmpty());

        // Exactly the limit is read; one byte more is not
        QByteArray big("[00:01.00]sana\n");
        big += QByteArray(Lyrics::maxFileBytes - big.size(), '\n');
        r = parseLrc(big);
        QCOMPARE(r.error, QString());
        QCOMPARE(wordCount(r), 1);
        big += '\n';
        r = parseLrc(big);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.lyrics.isEmpty());
    }

    // Frames are the seconds times the rate, rounded; a word with no
    // length gets one frame; the value is the line. Back again, the
    // same words come out, to the frame.
    void events_at_44100_and_48000() {
        LyricsParseResult r = parse("[00:01.234]<00:01.234>a <00:01.234>b <00:02.000>c\n"
                                    "[01:02.500]<01:02.500>d\n");
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 1.234, 1.234, "a", 0, true },
                { 1.234, 2.0, "b", 0, true },
                { 2.0, 2.0 + W, "c", 0, false },
                { 62.5, 62.5 + W, "d", 1, false },
            });
        if (QTest::currentTestFailed()) return;

        // 1.234 s is 54419.4 frames at 44100, and 59232 at 48000
        const sv::sv_frame_t w44 = sv::sv_frame_t(std::llround(W * 44100));
        const sv::sv_frame_t w48 = sv::sv_frame_t(std::llround(W * 48000));
        struct Case {
            double rate;
            sv::EventVector expected;
        } cases[] = {
            { 44100, {
                    sv::Event(54419, 0.f, 1, "a"),
                    sv::Event(54419, 0.f, 88200 - 54419, "b"),
                    sv::Event(88200, 0.f, w44, "c"),
                    sv::Event(2756250, 1.f, w44, "d"),
                } },
            { 48000, {
                    sv::Event(59232, 0.f, 1, "a"),
                    sv::Event(59232, 0.f, 96000 - 59232, "b"),
                    sv::Event(96000, 0.f, w48, "c"),
                    sv::Event(3000000, 1.f, w48, "d"),
                } },
        };

        for (const Case &c : cases) {
            sv::EventVector events = lyricsToEvents(r.lyrics, c.rate);
            QCOMPARE(int(events.size()), int(c.expected.size()));
            for (int i = 0; i < int(events.size()); ++i) {
                QVERIFY2(events[i] == c.expected[i],
                         qPrintable(events[i].toXmlString() + " expected " +
                                    c.expected[i].toXmlString()));
            }

            // Back from the events in any order, as a model would give
            // them: the same words, and the same events again
            sv::EventVector reversed(events.rbegin(), events.rend());
            Lyrics back = lyricsFromEvents(reversed, c.rate);
            QCOMPARE(back.words.size(), r.lyrics.words.size());
            for (int i = 0; i < int(back.words.size()); ++i) {
                const LyricWord &w = back.words[i];
                const LyricWord &o = r.lyrics.words[i];
                QCOMPARE(w.text, o.text);
                QCOMPARE(w.line, o.line);
                QVERIFY(std::abs(w.start - o.start) <= 0.5 / c.rate);
            }
            QVERIFY(lyricsToEvents(back, c.rate) == events);
        }
    }

    // Two words at the same frame are two events, even when nothing
    // tells them apart, and a model's event series keeps both
    void identical_words_at_one_frame_both_kept() {
        LyricsParseResult r = parse("[00:01.00]<00:01.00>la<00:01.00>la<00:01.00>\n");
        QCOMPARE(r.warnings, QStringList());
        compareWords(r.lyrics, {
                { 1.0, 1.0, "la", 0, true },
                { 1.0, 1.0, "la", 0, true },
            });

        sv::EventVector events = lyricsToEvents(r.lyrics, 44100);
        QCOMPARE(int(events.size()), 2);
        QVERIFY(events[0] == events[1]);

        sv::EventSeries series;
        for (const sv::Event &e : events) series.add(e);
        QCOMPARE(series.count(), 2);
    }

    // What Moises-Lyric-Exporter writes in word mode: no end times,
    // clamped stamps at 0, glued words and a gap marker
    void exporter_word_fixture() {
        QByteArray bytes = fixture("moises-exporter-words.lrc");
        QVERIFY(!bytes.isEmpty());
        LyricsParseResult r = parseLrc(bytes);
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        QCOMPARE(r.lyrics.title, QString::fromUtf8("Kesäyön testilaulu"));
        QVERIFY(r.lyrics.wordTimed);
        QCOMPARE(r.lyrics.lineCount(), 4);
        compareWords(r.lyrics, {
                { 0.0, 0.0, "Alku", 0, true },
                { 0.0, 0.31, "ennen", 0, true },
                { 0.31, 0.31 + W, "nollaa", 0, false },
                { 4.64, 4.92, "Tämä", 1, true },
                { 4.92, 5.16, "on", 1, true },
                { 5.16, 5.62, "keksitty", 1, true },
                { 5.62, 6.0, "laulu,", 1, true },
                { 21.5, 21.8, "Yö", 2, true },
                { 21.8, 22.05, "on", 2, true },
                { 22.05, 23.4, "hämärä", 2, false },
                { 23.4, 23.7, "Nyt", 3, true },
                { 23.7, 24.0, "se", 3, true },
                { 24.0, 24.0 + W, "loppuu!", 3, false },
            });
    }

    // The same in line mode, with two decimals
    void exporter_line_fixture() {
        QByteArray bytes = fixture("moises-exporter-lines.lrc");
        QVERIFY(!bytes.isEmpty());
        LyricsParseResult r = parseLrc(bytes);
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        QCOMPARE(r.lyrics.title, QString::fromUtf8("Kesäyön testilaulu"));
        QVERIFY(!r.lyrics.wordTimed);
        QCOMPARE(r.lyrics.lineCount(), 4);
        compareWords(r.lyrics, {
                { 0.0, 4.64, "Alku ennen nollaa", 0, false },
                { 4.64, 6.0, "Tämä on keksittylaulu,", 1, true },
                { 21.5, 23.4, "Yö on hämärä", 2, false },
                { 23.4, 23.4 + L, "Nyt seloppuu!", 3, false },
            });
    }

    // An enhanced LRC that gives its ends: trailing word tags and
    // empty lines
    void fixture_with_ends() {
        QByteArray bytes = fixture("lrc-with-ends.lrc");
        QVERIFY(!bytes.isEmpty());
        LyricsParseResult r = parseLrc(bytes);
        QCOMPARE(r.error, QString());
        QCOMPARE(r.warnings, QStringList());
        QCOMPARE(r.lyrics.title, QString::fromUtf8("Päivä [Live]"));
        QCOMPARE(r.lyrics.artist, QString::fromUtf8("Testiryhmä"));
        QVERIFY(r.lyrics.wordTimed);
        compareWords(r.lyrics, {
                { 1.0, 1.5, "Päivä", 0, true },
                { 1.5, 2.2, "paistaa", 0, true },
                { 3.0, 3.4, "Pöllö", 1, true },
                { 3.4, 4.8, "huhuilee", 1, true },
                { 6.0, 8.5, "Koko rivi yhdellä leimalla", 2, true },
            });
    }
};

#endif
