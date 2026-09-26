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

#include "LyricsTtml.h"

#include <QCoreApplication>
#include <QMap>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("Lyrics", text);
}

// "1 line was" or "3 lines were": the status bar shows these
QString counted(int n, const char *one, const char *many)
{
    if (n == 1) return tr(one);
    return tr(many).arg(n);
}

const char *const ttmlNamespace = "http://www.w3.org/ns/ttml";
const char *const itunesNamespace = "http://music.apple.com/lyric-ttml-internal";
const char *const metadataNamespace = "http://www.w3.org/ns/ttml#metadata";

// Only to keep the arithmetic far from overflowing, as the LRC parser
// has it: no reference is a day long
const qint64 maxSeconds = 24 * 3600;

// The biggest number a time can have in it: that day in milliseconds
const qint64 maxNumber = maxSeconds * 1000;

// Digits kept after the point: microseconds are plenty
const int maxFractionDigits = 6;

bool isAsciiDigit(QChar c)
{
    return c.unicode() >= '0' && c.unicode() <= '9';
}

int digitValue(QChar c)
{
    return c.unicode() - '0';
}

// What XML counts as white space; nothing else separates words
bool isXmlSpace(QChar c)
{
    const ushort u = c.unicode();
    return u == ' ' || u == '\t' || u == '\n' || u == '\r';
}

QStringView xmlTrimmed(QStringView s)
{
    qsizetype b = 0, e = s.size();
    while (b < e && isXmlSpace(s[b])) ++b;
    while (e > b && isXmlSpace(s[e - 1])) --e;
    return s.mid(b, e - b);
}

// Each line break or tab a space, so that the cleaning, which drops
// controls, does not glue the words either side of it
QString spaced(QStringView s)
{
    QString out = s.toString();
    for (QChar &c : out) {
        if (isXmlSpace(c)) c = QLatin1Char(' ');
    }
    return out;
}

// Every run of white space one space
QString collapsed(QStringView s)
{
    QString out;
    out.reserve(s.size());
    bool blank = false;
    for (QChar c : s) {
        if (isXmlSpace(c)) {
            blank = true;
            continue;
        }
        if (blank && !out.isEmpty()) out += QLatin1Char(' ');
        blank = false;
        out += c;
    }
    return out;
}

/**
 * At least one digit at i, which is moved past them; false if there
 * is none, or so many that they could be no time.
 */
bool readNumber(QStringView s, qsizetype &i, qint64 &value)
{
    const qsizetype from = i;
    value = 0;
    while (i < s.size() && isAsciiDigit(s[i])) {
        value = value * 10 + digitValue(s[i]);
        if (value > maxNumber) return false;
        ++i;
    }
    return i > from;
}

/**
 * A TTML time in seconds: a clock time [[h:]m:]s[.fraction], where
 * the minutes can pass 59 when there are no hours (the exporter writes
 * m:ss.mmm, AMLL TTML Tool mm:ss.mmm, Apple sometimes plain seconds),
 * or an offset time <number>h, m, s or ms.  Frames and ticks (f, t,
 * hh:mm:ss:ff) are not read: nothing is returned for them, as for
 * anything else.
 */
std::optional<double> readTime(QStringView text)
{
    const QStringView s = xmlTrimmed(text);
    const qsizetype n = s.size();
    qsizetype i = 0;

    qint64 fields[3];
    int count = 0;
    while (true) {
        if (!readNumber(s, i, fields[count])) return {};
        ++count;
        if (count < 3 && i < n && s[i] == QLatin1Char(':')) {
            ++i;
            continue;
        }
        break;
    }

    qint64 fraction = 0;
    qint64 scale = 1;
    if (i < n && s[i] == QLatin1Char('.')) {
        ++i;
        int digits = 0;
        while (i < n && isAsciiDigit(s[i])) {
            if (digits < maxFractionDigits) {
                fraction = fraction * 10 + digitValue(s[i]);
                scale *= 10;
            }
            ++digits;
            ++i;
        }
        if (digits == 0) return {};
    }

    // The number is in units of multiplier / divisor seconds
    qint64 whole = 0;
    qint64 multiplier = 1;
    qint64 divisor = 1;
    const QStringView metric = s.mid(i);

    if (count == 1) {
        whole = fields[0];
        if (metric.isEmpty() || metric == QLatin1String("s")) {
        } else if (metric == QLatin1String("ms")) {
            divisor = 1000;
        } else if (metric == QLatin1String("m")) {
            multiplier = 60;
        } else if (metric == QLatin1String("h")) {
            multiplier = 3600;
        } else {
            return {};
        }
    } else {
        if (!metric.isEmpty()) return {};
        const qint64 hours = (count == 3 ? fields[0] : 0);
        const qint64 minutes = fields[count - 2];
        const qint64 seconds = fields[count - 1];
        if (seconds >= 60) return {};
        if (count == 3 && minutes >= 60) return {};
        whole = (hours * 60 + minutes) * 60 + seconds;
    }

    if (whole > maxSeconds * divisor / multiplier) return {};

    // Whole milliseconds, as nearly every file has them, come out as
    // the nearest double to the decimal, not a step away from it
    const qint64 units = whole * scale + fraction;
    const double result =
        double(units) * double(multiplier) / (double(scale) * double(divisor));
    if (result > double(maxSeconds)) return {};
    return result;
}

// What was skipped or changed, for the warnings
struct Counts
{
    int unreadableBegins = 0;
    int unreadableEnds = 0;
    int untimedLines = 0;
    int droppedText = 0;
    int skippedParts = 0;
    int backwards = 0;
};

struct Timing
{
    /// There is a begin, whether or not it could be read
    bool hasBegin = false;

    std::optional<double> begin;

    /// The end, or else begin + dur
    std::optional<double> end;
};

/**
 * An element's begin, end and dur, matched by local name in any
 * namespace.  An end that cannot be read is counted only when the
 * begin can: a word is left out for its begin, with a warning of its
 * own, but a line with timed words needs no begin.
 */
Timing readTiming(const QXmlStreamAttributes &attributes, Counts &counts)
{
    std::optional<QStringView> begin, end, dur;
    for (const QXmlStreamAttribute &a : attributes) {
        const QStringView name = a.name();
        if (name == QLatin1String("begin")) {
            if (!begin) begin = a.value();
        } else if (name == QLatin1String("end")) {
            if (!end) end = a.value();
        } else if (name == QLatin1String("dur")) {
            if (!dur) dur = a.value();
        }
    }

    Timing timing;
    if (begin) {
        timing.hasBegin = true;
        timing.begin = readTime(*begin);
    }

    if (end) {
        timing.end = readTime(*end);
        if (!timing.end && (timing.begin || !timing.hasBegin)) {
            ++counts.unreadableEnds;
        }
    }
    if (!timing.end && dur && timing.begin) {
        std::optional<double> length = readTime(*dur);
        if (length) {
            timing.end = *timing.begin + *length;
        } else {
            ++counts.unreadableEnds;
        }
    }
    return timing;
}

/**
 * Background vocals, translations and romanisations: AMLL TTML Tool
 * puts them in the line as spans with these roles.  ttm:role is a
 * list, in TTML 1.
 */
bool hasSkippedRole(const QXmlStreamAttributes &attributes)
{
    static const QStringList skipped = {
        QStringLiteral("x-bg"), QStringLiteral("x-translation"),
        QStringLiteral("x-roman"), QStringLiteral("x-romanization")
    };
    for (const QXmlStreamAttribute &a : attributes) {
        if (a.name() != QLatin1String("role")) continue;
        // An attribute's line breaks and tabs are read as spaces
        const QStringList roles =
            a.value().toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &role : roles) {
            if (skipped.contains(role)) return true;
        }
    }
    return false;
}

// What a line holds, in file order, before it is made into words
struct Piece
{
    enum Kind {
        Timed,          ///< A span with a begin
        Text,           ///< Text outside timed spans, with no blanks
        Blank           ///< White space, a <br/> or a part left out
    };

    Kind kind = Blank;
    QString text;
    double begin = 0.0;
    std::optional<double> end;
};

void addBlank(QVector<Piece> &pieces)
{
    if (!pieces.isEmpty() && pieces.last().kind == Piece::Blank) return;
    pieces.push_back(Piece());
}

void addText(QStringView text, QVector<Piece> &pieces)
{
    const qsizetype n = text.size();
    qsizetype i = 0;
    while (i < n) {
        const bool blank = isXmlSpace(text[i]);
        qsizetype j = i;
        while (j < n && isXmlSpace(text[j]) == blank) ++j;
        if (blank) {
            addBlank(pieces);
        } else if (!pieces.isEmpty() && pieces.last().kind == Piece::Text) {
            // The reader may hand one run of text over in parts
            pieces.last().text += text.mid(i, j - i);
        } else {
            Piece piece;
            piece.kind = Piece::Text;
            piece.text = text.mid(i, j - i).toString();
            pieces.push_back(piece);
        }
        i = j;
    }
}

/**
 * All the text in the element the reader is at, which is read to its
 * end.  A <br/> is a space, and so is a skipped part, so that the
 * words either side of it are not joined; elements other than spans
 * (metadata, animation) hold no lyrics and are passed over.
 */
QString readAllText(QXmlStreamReader &reader, Counts &counts)
{
    QString text;
    int depth = 1;
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::Characters:
            text += reader.text();
            break;
        case QXmlStreamReader::StartElement:
            if (reader.name() == QLatin1String("br")) {
                text += QLatin1Char(' ');
                reader.skipCurrentElement();
            } else if (reader.name() != QLatin1String("span")) {
                reader.skipCurrentElement();
            } else if (hasSkippedRole(reader.attributes())) {
                ++counts.skippedParts;
                text += QLatin1Char(' ');
                reader.skipCurrentElement();
            } else {
                ++depth;
            }
            break;
        case QXmlStreamReader::EndElement:
            if (--depth == 0) return text;
            break;
        default:
            break;
        }
    }
    return text;
}

/**
 * The content of the element the reader is at, a <p> or a wrapping
 * <span>, read to its end.
 */
void readInline(QXmlStreamReader &reader, QVector<Piece> &pieces,
                Counts &counts)
{
    while (!reader.atEnd()) {

        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::EndElement) return;
        if (token == QXmlStreamReader::Characters) {
            addText(reader.text(), pieces);
            continue;
        }
        if (token != QXmlStreamReader::StartElement) continue;

        if (reader.name() == QLatin1String("br")) {
            addBlank(pieces);
            reader.skipCurrentElement();
            continue;
        }
        if (reader.name() != QLatin1String("span")) {
            reader.skipCurrentElement();
            continue;
        }

        const QXmlStreamAttributes attributes = reader.attributes();

        if (hasSkippedRole(attributes)) {
            // It can come straight after a word, and a word after it
            ++counts.skippedParts;
            addBlank(pieces);
            reader.skipCurrentElement();
            continue;
        }

        const Timing timing = readTiming(attributes, counts);

        if (!timing.hasBegin) {
            // A wrapper: its content is read as if it were not there
            readInline(reader, pieces, counts);
            continue;
        }

        const QString text = readAllText(reader, counts);

        if (!timing.begin) {
            ++counts.unreadableBegins;
            addBlank(pieces);
            continue;
        }

        // Blanks at either end of its text are blanks between words
        if (!text.isEmpty() && isXmlSpace(text.front())) addBlank(pieces);
        Piece piece;
        piece.kind = Piece::Timed;
        piece.text = xmlTrimmed(text).toString();
        piece.begin = *timing.begin;
        piece.end = timing.end;
        pieces.push_back(piece);
        if (!text.isEmpty() && isXmlSpace(text.back())) addBlank(pieces);
    }
}

// A word before it has its line index, and perhaps its end
struct Entry
{
    double start = 0.0;
    std::optional<double> end;
    bool endGiven = false;
    QString text;
};

struct Line
{
    /// In time order
    QVector<Entry> words;

    /// The <p>'s own end, if it has one
    std::optional<double> end;

    /// It had no timed spans: its one word is the whole line
    bool lineTimed = false;
};

/**
 * The <p> the reader is at, read to its end: one line, unless nothing
 * in it is timed and it has no begin either, or it has no text.
 */
void readParagraph(QXmlStreamReader &reader, QVector<Line> &lines,
                   Counts &counts)
{
    const Timing timing = readTiming(reader.attributes(), counts);

    QVector<Piece> pieces;
    readInline(reader, pieces, counts);

    Line line;
    line.end = timing.end;

    const bool anyTimed =
        std::any_of(pieces.begin(), pieces.end(), [](const Piece &p) {
                        return p.kind == Piece::Timed;
                    });

    if (!anyTimed) {
        QString text;
        for (const Piece &p : pieces) {
            text += (p.kind == Piece::Blank ? QStringLiteral(" ") : p.text);
        }
        const QString label = lyricsLabel(collapsed(text));
        if (label.isEmpty()) return;
        if (timing.hasBegin && !timing.begin) {
            ++counts.unreadableBegins;
            return;
        }
        if (!timing.begin) {
            ++counts.untimedLines;
            return;
        }
        Entry entry;
        entry.start = *timing.begin;
        entry.end = timing.end;
        entry.text = label;
        line.words.push_back(entry);
        line.lineTimed = true;
        lines.push_back(line);
        return;
    }

    // Timed spans with nothing but text between them are one word:
    // syllables, or a word and the punctuation after it
    std::optional<Entry> current;
    auto finish = [&]() {
        if (!current) return;
        current->text = lyricsLabel(spaced(current->text));
        if (!current->text.isEmpty()) line.words.push_back(*current);
        current.reset();
    };

    for (const Piece &p : pieces) {
        switch (p.kind) {
        case Piece::Blank:
            finish();
            break;
        case Piece::Timed:
            if (current) {
                current->text += p.text;
                current->end = p.end;
            } else {
                Entry entry;
                entry.start = p.begin;
                entry.end = p.end;
                entry.text = p.text;
                current = entry;
            }
            break;
        case Piece::Text:
            if (current) {
                current->text += p.text;
            } else {
                ++counts.droppedText;
            }
            break;
        }
    }
    finish();

    if (line.words.isEmpty()) return;

    std::stable_sort(line.words.begin(), line.words.end(),
                     [](const Entry &a, const Entry &b) {
                         return a.start < b.start;
                     });
    lines.push_back(line);
}

/**
 * Where each word ends when the file does not say: at the next word
 * of its line, else at the end of the line, else as the LRC parser
 * has it, a while after its start but not past the next line's start
 * (a line-timed line: at the next line's start).  The lines must be in
 * time order.
 */
void inferEnds(QVector<Line> &lines, Counts &counts)
{
    for (int k = 0; k < lines.size(); ++k) {
        Line &line = lines[k];
        for (int i = 0; i < line.words.size(); ++i) {
            Entry &e = line.words[i];
            if (e.end) {
                e.endGiven = true;
            } else if (i + 1 < line.words.size()) {
                e.end = line.words[i + 1].start;
            } else if (line.end) {
                e.end = line.end;
                e.endGiven = true;
            } else if (k + 1 < lines.size()) {
                const double next = lines[k + 1].words[0].start;
                if (line.lineTimed) {
                    e.end = next;
                } else {
                    e.end = std::min(next, e.start +
                                     Lyrics::inferredWordSeconds);
                }
            } else {
                e.end = e.start + (line.lineTimed ?
                                   Lyrics::inferredLastLineSeconds :
                                   Lyrics::inferredWordSeconds);
            }

            if (*e.end < e.start) {
                if (e.endGiven) ++counts.backwards;
                e.end = e.start;
            }
        }
    }
}

// Seconds as m:ss.mmm, to the nearest millisecond
QString ttmlTime(double seconds)
{
    const qint64 ms = std::max(qint64(0), qint64(std::llround(seconds * 1000)));
    return QString("%1:%2.%3")
        .arg(ms / 60000)
        .arg((ms % 60000) / 1000, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

} // namespace

LyricsParseResult
parseTtml(const QByteArray &bytes)
{
    LyricsParseResult result;

    if (bytes.isEmpty()) {
        result.error = tr("The file is empty.");
        return result;
    }
    if (bytes.size() > Lyrics::maxFileBytes) {
        result.error = tr("The file is over 1 MB, too big to be a TTML "
                          "lyrics file.");
        return result;
    }

    // The reader finds the encoding from the BOM or the declaration
    QXmlStreamReader reader(bytes);

    Counts counts;
    QVector<Line> lines;
    QString title;
    bool inHead = false;

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();

        if (token == QXmlStreamReader::DTD) {
            result.error = tr("The file has a document type declaration "
                              "(<!DOCTYPE>), which a TTML file never has, "
                              "so it was not read.");
            return result;
        }

        if (token == QXmlStreamReader::StartElement) {
            const QStringView name = reader.name();
            if (name == QLatin1String("head")) {
                inHead = true;
            } else if (inHead) {
                if (name == QLatin1String("title") && title.isEmpty()) {
                    title = lyricsLabel(collapsed
                                        (reader.readElementText
                                         (QXmlStreamReader::IncludeChildElements)));
                }
            } else if (name == QLatin1String("p")) {
                readParagraph(reader, lines, counts);
            }
        } else if (token == QXmlStreamReader::EndElement &&
                   reader.name() == QLatin1String("head")) {
            inHead = false;
        }
    }

    if (reader.hasError()) {
        result.error = tr("The file is not well-formed XML: %1 (line %2).")
            .arg(reader.errorString()).arg(reader.lineNumber());
        return result;
    }

    // Lines in time order; those that start together stay in file order
    std::stable_sort(lines.begin(), lines.end(),
                     [](const Line &a, const Line &b) {
                         return a.words[0].start < b.words[0].start;
                     });

    inferEnds(lines, counts);

    Lyrics &lyrics = result.lyrics;
    lyrics.title = title;
    for (int k = 0; k < lines.size(); ++k) {
        if (!lines[k].lineTimed) lyrics.wordTimed = true;
        for (const Entry &e : lines[k].words) {
            LyricWord word;
            word.start = e.start;
            word.end = *e.end;
            word.text = e.text;
            word.line = k;
            word.endGiven = e.endGiven;
            lyrics.words.push_back(word);
        }
    }

    std::stable_sort(lyrics.words.begin(), lyrics.words.end(),
                     [](const LyricWord &a, const LyricWord &b) {
                         return a.start < b.start;
                     });

    if (counts.unreadableBegins > 0) {
        result.warnings << counted
            (counts.unreadableBegins,
             "1 word had a start time that could not be read and was "
             "left out.",
             "%1 words had start times that could not be read and were "
             "left out.");
    }
    if (counts.unreadableEnds > 0) {
        result.warnings << counted
            (counts.unreadableEnds,
             "1 end time could not be read and was ignored.",
             "%1 end times could not be read and were ignored.");
    }
    if (counts.untimedLines > 0) {
        result.warnings << counted
            (counts.untimedLines,
             "1 line had no time and was left out.",
             "%1 lines had no time and were left out.");
    }
    if (counts.droppedText > 0) {
        result.warnings << counted
            (counts.droppedText,
             "1 piece of text outside the timed words was left out.",
             "%1 pieces of text outside the timed words were left out.");
    }
    if (counts.skippedParts > 0) {
        result.warnings << counted
            (counts.skippedParts,
             "1 background vocal, translation or romanisation was "
             "skipped.",
             "%1 background vocals, translations or romanisations were "
             "skipped.");
    }
    if (counts.backwards > 0) {
        result.warnings << counted
            (counts.backwards,
             "1 word ended before it started and was given no length.",
             "%1 words ended before they started and were given no "
             "length.");
    }

    if (lyrics.words.isEmpty()) {
        result.lyrics = Lyrics();
        result.error = tr("No timed lyrics were found: this is not a TTML "
                          "file, or it has no timed lines.");
    }

    return result;
}

QByteArray
writeTtml(const Lyrics &lyrics)
{
    const QString tt = QString::fromLatin1(ttmlNamespace);
    const QString itunes = QString::fromLatin1(itunesNamespace);
    const QString ttm = QString::fromLatin1(metadataNamespace);

    // A <p> for each line that has words, in line order
    QMap<int, QVector<LyricWord>> lines;
    for (const LyricWord &w : lyrics.words) lines[w.line].push_back(w);

    double first = 0.0;
    double last = 0.0;
    for (int i = 0; i < lyrics.words.size(); ++i) {
        const LyricWord &w = lyrics.words[i];
        if (i == 0 || w.start < first) first = w.start;
        last = std::max(last, std::max(w.start, w.end));
    }

    QByteArray out;
    QXmlStreamWriter writer(&out);

    // Laid out by hand, as the exporter lays it out: automatic
    // formatting would put white space between the spans of a line
    auto newline = [&](int indent) {
        writer.writeCharacters(QLatin1String("\n") +
                               QString(indent * 2, QLatin1Char(' ')));
    };

    writer.writeStartDocument();
    newline(0);

    writer.writeDefaultNamespace(tt);
    writer.writeNamespace(itunes, QStringLiteral("itunes"));
    writer.writeNamespace(ttm, QStringLiteral("ttm"));
    writer.writeStartElement(tt, QStringLiteral("tt"));
    writer.writeAttribute(itunes, QStringLiteral("timing"),
                          QStringLiteral("Word"));

    newline(1);
    writer.writeStartElement(tt, QStringLiteral("head"));
    newline(2);
    writer.writeStartElement(tt, QStringLiteral("metadata"));
    if (!lyrics.title.isEmpty()) {
        newline(3);
        writer.writeTextElement(ttm, QStringLiteral("title"), lyrics.title);
    }
    newline(3);
    writer.writeEmptyElement(ttm, QStringLiteral("agent"));
    writer.writeAttribute(QStringLiteral("type"), QStringLiteral("person"));
    writer.writeAttribute(QStringLiteral("xml:id"), QStringLiteral("v1"));
    newline(2);
    writer.writeEndElement(); // metadata
    newline(1);
    writer.writeEndElement(); // head

    newline(1);
    writer.writeStartElement(tt, QStringLiteral("body"));
    writer.writeAttribute(QStringLiteral("dur"), ttmlTime(last));
    newline(2);
    writer.writeStartElement(tt, QStringLiteral("div"));
    writer.writeAttribute(QStringLiteral("begin"), ttmlTime(first));
    writer.writeAttribute(QStringLiteral("end"), ttmlTime(last));

    int key = 0;
    for (auto i = lines.begin(); i != lines.end(); ++i) {

        QVector<LyricWord> words = i.value();
        std::stable_sort(words.begin(), words.end(),
                         [](const LyricWord &a, const LyricWord &b) {
                             return a.start < b.start;
                         });
        double begin = words[0].start;
        double end = words[0].end;
        for (const LyricWord &w : words) {
            end = std::max(end, w.end);
        }

        newline(3);
        writer.writeStartElement(tt, QStringLiteral("p"));
        writer.writeAttribute(QStringLiteral("begin"), ttmlTime(begin));
        writer.writeAttribute(QStringLiteral("end"), ttmlTime(end));
        writer.writeAttribute(ttm, QStringLiteral("agent"),
                              QStringLiteral("v1"));
        writer.writeAttribute(itunes, QStringLiteral("key"),
                              QStringLiteral("L%1").arg(++key));

        for (int j = 0; j < words.size(); ++j) {
            if (j > 0) writer.writeCharacters(QStringLiteral(" "));
            writer.writeStartElement(tt, QStringLiteral("span"));
            writer.writeAttribute(QStringLiteral("begin"),
                                  ttmlTime(words[j].start));
            writer.writeAttribute(QStringLiteral("end"),
                                  ttmlTime(words[j].end));
            writer.writeCharacters(words[j].text);
            writer.writeEndElement();
        }

        writer.writeEndElement(); // p
    }

    newline(2);
    writer.writeEndElement(); // div
    newline(1);
    writer.writeEndElement(); // body
    newline(0);
    writer.writeEndElement(); // tt
    writer.writeEndDocument();

    // Qt ends the document with a line break; not every version need
    if (!out.endsWith('\n')) out += '\n';

    return out;
}
