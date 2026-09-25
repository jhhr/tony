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

#include "Lyrics.h"

#include <QCoreApplication>
#include <QStringConverter>

#include <algorithm>
#include <cmath>
#include <optional>

using namespace sv;

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

// Every time in an LRC file is a whole number of milliseconds, and so
// is the offset, so they stay that way until the words are made: no
// rounding can then put a word before the one it follows
typedef qint64 Ms;

const Ms msPerSecond = 1000;

// Only to keep the arithmetic far from overflowing: no reference is a
// day long
const qint64 maxMinutes = 24 * 60;

bool isAsciiDigit(QChar c)
{
    return c.unicode() >= '0' && c.unicode() <= '9';
}

int digitValue(QChar c)
{
    return c.unicode() - '0';
}

/**
 * The length of the time tag at pos, or 0 if there is none there.  A
 * tag is [m:ss], [m:ss.f], [m:ss.ff] or [m:ss.fff], with ':' allowed in
 * place of '.', and with open and close in place of the brackets: '<'
 * and '>' for a word.  Seconds of 60 or more make it no tag.
 */
int readTimeTag(const QString &s, int pos, char open, char close, Ms &time)
{
    const int n = s.size();
    int i = pos;
    if (i >= n || s[i] != QLatin1Char(open)) return 0;
    ++i;

    qint64 minutes = 0;
    int minuteDigits = 0;
    while (i < n && isAsciiDigit(s[i])) {
        minutes = minutes * 10 + digitValue(s[i]);
        if (minutes > maxMinutes) return 0;
        ++minuteDigits;
        ++i;
    }
    if (minuteDigits == 0 || i >= n || s[i] != QLatin1Char(':')) return 0;
    ++i;

    if (i + 1 >= n || !isAsciiDigit(s[i]) || !isAsciiDigit(s[i + 1])) {
        return 0;
    }
    int seconds = digitValue(s[i]) * 10 + digitValue(s[i + 1]);
    if (seconds >= 60) return 0;
    i += 2;

    Ms fraction = 0;
    if (i < n && (s[i] == QLatin1Char('.') || s[i] == QLatin1Char(':'))) {
        ++i;
        int fractionDigits = 0;
        while (i < n && isAsciiDigit(s[i]) && fractionDigits < 3) {
            fraction = fraction * 10 + digitValue(s[i]);
            ++fractionDigits;
            ++i;
        }
        if (fractionDigits == 0) return 0;
        // One digit is tenths, two hundredths, three thousandths
        for (int d = fractionDigits; d < 3; ++d) fraction *= 10;
    }

    if (i >= n || s[i] != QLatin1Char(close)) return 0;
    ++i;

    time = (minutes * 60 + seconds) * msPerSecond + fraction;
    return i - pos;
}

/**
 * A [key:value] line.  A key of digits only is a time tag gone wrong,
 * not metadata.  The value runs to the last ']', so that it can hold
 * brackets of its own: [ti:Song [Live]].
 */
bool readMetadata(const QString &row, QString &key, QString &value)
{
    if (!row.startsWith(QLatin1Char('[')) || !row.endsWith(QLatin1Char(']'))) {
        return false;
    }
    int colon = row.indexOf(QLatin1Char(':'));
    if (colon < 0) return false;

    QString k = row.mid(1, colon - 1).trimmed();
    if (k.isEmpty() || k.contains(QLatin1Char('[')) ||
        k.contains(QLatin1Char(']'))) {
        return false;
    }
    if (std::all_of(k.begin(), k.end(), isAsciiDigit)) return false;

    key = k.toLower();
    value = row.mid(colon + 1, row.size() - colon - 2).trimmed();
    return true;
}

/**
 * XML 1.0 cannot hold the C0 controls other than tab (and the line
 * breaks, which never get this far), nor U+FFFE and U+FFFF, which the
 * UTF-8 decoder lets through: one in a label would make the session
 * file unreadable.  DEL is never meant as text either.  A tab becomes
 * a space, which is what it comes back as from a session file anyway:
 * an XML attribute value is read with its tabs as spaces.
 */
QString withoutControls(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        const ushort u = c.unicode();
        if (u == '\t') {
            out += QLatin1Char(' ');
            continue;
        }
        if (u < 0x20 || u == 0x7F || u == 0xFFFE || u == 0xFFFF) {
            continue;
        }
        out += c;
    }
    return out;
}

// A label as it is shown and saved: trimmed, and not too long
QString labelFrom(const QString &text)
{
    QString s = text.trimmed();
    if (s.size() > Lyrics::maxLabelLength) {
        int n = Lyrics::maxLabelLength;
        // Never half of a surrogate pair
        if (s.at(n - 1).isHighSurrogate()) --n;
        s = s.left(n).trimmed();
    }
    return s;
}

// Nothing but notes and space: the exporter's mark for a gap
bool isOnlyMusic(const QString &text)
{
    for (QChar c : text) {
        if (c != QChar(0x266A) && c != QChar(0x266B) && !c.isSpace()) {
            return false;
        }
    }
    return true;
}

/**
 * The text of the file.  A BOM says what it is; without one it ought
 * to be UTF-8, and anything that is not is read as Latin-1.  That
 * decodes every byte, keeps the ä and ö of an older Finnish file, and
 * gives the same answer on every system, as the system's own codec
 * would not.  Stateless, so that a sequence cut off at the end counts
 * as an error too.
 */
QString decoded(const QByteArray &bytes, QStringList &warnings)
{
    const QStringConverter::Flags flags = QStringConverter::Flag::Stateless;

    std::optional<QStringConverter::Encoding> bom =
        QStringConverter::encodingForData(bytes);
    if (bom) {
        QStringDecoder decoder(*bom, flags);
        QString text = decoder.decode(bytes);
        if (decoder.hasError()) {
            warnings << tr("Some characters in the file could not be read "
                           "and were replaced.");
        }
        return text;
    }

    QStringDecoder utf8(QStringConverter::Utf8, flags);
    QString text = utf8.decode(bytes);
    if (!utf8.hasError()) return text;

    warnings << tr("The file is not UTF-8, so it was read as Latin-1.");
    QStringDecoder latin1(QStringConverter::Latin1, flags);
    return latin1.decode(bytes);
}

// A word, or a whole line, before it has a line index
struct Entry
{
    Ms start = 0;
    Ms end = 0;
    bool haveEnd = false;
    bool endGiven = false;
    QString text;
};

struct TimedLine
{
    Ms stamp = 0;

    /// Its entries are words, not the whole line
    bool hasWordTags = false;

    /// None: the line only marks where the one before it ends
    QVector<Entry> entries;
};

/**
 * The entries of the text after a line's time tags.  Words are split
 * on word tags only, never on spaces: the exporter leaves the space
 * out before a word with punctuation in it (onze<t>stilo,).  Each tag
 * starts a word and ends the one before it; a tag with no text after
 * it only ends the one before.  Text before the first tag starts at
 * the line's own time.
 */
TimedLine readLineText(const QString &text, Ms stamp, int &backwards)
{
    TimedLine line;
    line.stamp = stamp;

    struct Piece {
        Ms time;
        bool tagged;
        QString text;
    };

    QVector<Piece> pieces;
    Piece current { stamp, false, QString() };
    const int n = text.size();
    int i = 0;
    while (i < n) {
        Ms time = 0;
        int length = readTimeTag(text, i, '<', '>', time);
        if (length > 0) {
            pieces.push_back(current);
            current = Piece { time, true, QString() };
            line.hasWordTags = true;
            i += length;
        } else {
            // Anything else, a '<' that starts no tag included, is text
            current.text += text[i];
            ++i;
        }
    }
    pieces.push_back(current);

    for (const Piece &piece : pieces) {
        Ms time = piece.time;
        if (piece.tagged && !line.entries.isEmpty()) {
            Entry &previous = line.entries.last();
            if (time < previous.start) {
                time = previous.start;
                ++backwards;
            }
            if (!previous.haveEnd) {
                previous.end = time;
                previous.haveEnd = true;
                previous.endGiven = true;
            }
        }
        QString label = labelFrom(piece.text);
        if (label.isEmpty()) continue;
        Entry entry;
        entry.start = time;
        entry.text = label;
        line.entries.push_back(entry);
    }

    bool onlyMusic = true;
    for (const Entry &e : line.entries) {
        if (!isOnlyMusic(e.text)) onlyMusic = false;
    }
    if (onlyMusic) line.entries.clear();

    return line;
}

TimedLine shifted(TimedLine line, Ms by)
{
    line.stamp += by;
    for (Entry &e : line.entries) {
        e.start += by;
        e.end += by;
    }
    return line;
}

/**
 * Where each line's last word ends when the line itself does not say:
 * where a following empty or music-only line puts it, else at the
 * next line, but a word no more than a short while after its start.
 * The lines must be in time order.
 */
void inferEnds(QVector<TimedLine> &lines)
{
    const Ms wordMs = Ms(std::llround(Lyrics::inferredWordSeconds *
                                      msPerSecond));
    const Ms lastLineMs = Ms(std::llround(Lyrics::inferredLastLineSeconds *
                                          msPerSecond));

    for (int k = 0; k < lines.size(); ++k) {

        if (lines[k].entries.isEmpty()) continue;

        // The words before it all end where the next word tag is
        Entry &last = lines[k].entries.last();
        if (last.haveEnd) continue;

        const bool isWord = lines[k].hasWordTags;

        if (k + 1 < lines.size()) {
            const TimedLine &next = lines[k + 1];
            if (next.entries.isEmpty()) {
                last.end = next.stamp;
                last.endGiven = true;
            } else if (isWord) {
                last.end = std::min(next.stamp, last.start + wordMs);
            } else {
                last.end = next.stamp;
            }
        } else {
            last.end = last.start + (isWord ? wordMs : lastLineMs);
        }

        // A line whose words run past the next line's start: this one
        // gets no length, which conversion makes one frame
        last.end = std::max(last.end, last.start);
        last.haveEnd = true;
    }
}

} // namespace

int
Lyrics::lineCount() const
{
    // Words need not be in line order: lines can overlap
    int count = 0;
    for (const LyricWord &w : words) count = std::max(count, w.line + 1);
    return count;
}

LyricsParseResult
parseLrc(const QByteArray &bytes)
{
    LyricsParseResult result;

    if (bytes.isEmpty()) {
        result.error = tr("The file is empty.");
        return result;
    }
    if (bytes.size() > Lyrics::maxFileBytes) {
        result.error = tr("The file is over 1 MB, too big to be an LRC file.");
        return result;
    }

    QString text = decoded(bytes, result.warnings);
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    Lyrics &lyrics = result.lyrics;
    QVector<TimedLine> lines;
    Ms offset = 0;
    int unrecognised = 0;
    int backwards = 0;

    const QStringList rows = text.split(QLatin1Char('\n'));
    for (const QString &raw : rows) {

        const QString row = withoutControls(raw).trimmed();
        if (row.isEmpty()) continue;

        QVector<Ms> stamps;
        int pos = 0;
        Ms stamp = 0;
        while (int length = readTimeTag(row, pos, '[', ']', stamp)) {
            stamps.push_back(stamp);
            pos += length;
        }

        if (!stamps.isEmpty()) {
            TimedLine line = readLineText(row.mid(pos), stamps[0], backwards);
            if (line.hasWordTags) lyrics.wordTimed = true;
            // [00:12.00][01:30.00]chorus: the line again at each stamp,
            // its word tags moving with it
            for (Ms s : stamps) {
                lines.push_back(shifted(line, s - stamps[0]));
            }
            continue;
        }

        QString key, value;
        if (readMetadata(row, key, value)) {
            if (key == QLatin1String("offset")) {
                bool ok = false;
                int ms = value.toInt(&ok);
                if (ok) {
                    offset = ms;
                } else {
                    result.warnings << tr("The offset \"%1\" is not a whole "
                                          "number of milliseconds and was "
                                          "ignored.").arg(labelFrom(value));
                }
            } else if (key == QLatin1String("ti")) {
                lyrics.title = labelFrom(value);
            } else if (key == QLatin1String("ar")) {
                lyrics.artist = labelFrom(value);
            }
            continue;
        }

        ++unrecognised;
    }

    // The exporter does not promise stamps in order, and it clamps
    // negative times to 0, so that several lines can share stamp 0:
    // those stay in file order
    std::stable_sort(lines.begin(), lines.end(),
                     [](const TimedLine &a, const TimedLine &b) {
                         return a.stamp < b.stamp;
                     });

    inferEnds(lines);

    // A positive offset makes the lyrics appear sooner, as LRC has it
    const Ms shift = -offset;

    int dropped = 0;
    int lineIndex = 0;
    for (const TimedLine &line : lines) {
        bool any = false;
        for (const Entry &e : line.entries) {
            if (e.start + shift < 0) {
                ++dropped;
                continue;
            }
            LyricWord word;
            word.start = double(e.start + shift) / msPerSecond;
            word.end = double(e.end + shift) / msPerSecond;
            word.text = e.text;
            word.line = lineIndex;
            word.endGiven = e.endGiven;
            lyrics.words.push_back(word);
            any = true;
        }
        if (any) ++lineIndex;
    }

    std::stable_sort(lyrics.words.begin(), lyrics.words.end(),
                     [](const LyricWord &a, const LyricWord &b) {
                         return a.start < b.start;
                     });

    if (unrecognised > 0) {
        result.warnings << counted
            (unrecognised,
             "1 line was not LRC and was skipped.",
             "%1 lines were not LRC and were skipped.");
    }
    if (backwards > 0) {
        result.warnings << counted
            (backwards,
             "1 word tag went back in time and was moved up to the word "
             "before it.",
             "%1 word tags went back in time and were moved up to the "
             "word before them.");
    }
    if (dropped > 0) {
        result.warnings << counted
            (dropped,
             "1 word came before the start of the song after the offset "
             "and was dropped.",
             "%1 words came before the start of the song after the offset "
             "and were dropped.");
    }

    if (lyrics.words.isEmpty()) {
        result.lyrics = Lyrics();
        result.error = tr("No timed lyrics were found: this is not an LRC "
                          "file, or it has no timed lines.");
    }

    return result;
}

EventVector
lyricsToEvents(const Lyrics &lyrics, sv_samplerate_t rate)
{
    EventVector events;
    for (const LyricWord &w : lyrics.words) {
        sv_frame_t frame = sv_frame_t(std::llround(w.start * rate));
        sv_frame_t end = sv_frame_t(std::llround(w.end * rate));
        sv_frame_t duration = std::max(sv_frame_t(1), end - frame);
        // Two words at the same frame are two events: a model keeps
        // both, whether or not their labels differ
        events.push_back(Event(frame, float(w.line), duration, w.text));
    }
    std::sort(events.begin(), events.end());
    return events;
}

Lyrics
lyricsFromEvents(const EventVector &events, sv_samplerate_t rate)
{
    Lyrics lyrics;
    if (rate <= 0) return lyrics;

    // In time order, then in line order, as the parser leaves them.
    // Words of one line at one frame go as a model orders them,
    // shortest first: all but the last of them end at that frame, so
    // this is the parser's order too, but for a tie.  The order the
    // events come in makes no difference.
    EventVector sorted(events);
    std::sort(sorted.begin(), sorted.end(),
              [](const Event &a, const Event &b) {
                  if (a.getFrame() != b.getFrame()) {
                      return a.getFrame() < b.getFrame();
                  }
                  if (a.getValue() != b.getValue()) {
                      return a.getValue() < b.getValue();
                  }
                  return a < b;
              });

    for (const Event &e : sorted) {
        LyricWord word;
        word.start = double(e.getFrame()) / rate;
        word.end = double(e.getFrame() + e.getDuration()) / rate;
        word.text = e.getLabel();
        word.line = int(std::lround(e.getValue()));
        lyrics.words.push_back(word);
    }
    return lyrics;
}
