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

#ifndef TONY_LYRICS_H
#define TONY_LYRICS_H

#include "base/BaseTypes.h"
#include "base/Event.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Timed lyrics read from an LRC file, and the events of the region
 * model that holds them in a session: one region per word (or per
 * line, for a file that times only lines), with the line's index as
 * its value.
 *
 * These are pure functions over bytes and event lists: they touch no
 * model, so they can be tested without a window (TestLyrics).
 */

/**
 * One word of the lyrics, or a whole line when the file times only
 * lines.
 */
struct LyricWord
{
    /// Seconds on the reference's timeline, after the file's [offset:]
    double start = 0.0;

    /// Seconds; never before start, but it can be start itself, for a
    /// word the next one starts at the same time as.  Conversion gives
    /// such a word one frame
    double end = 0.0;

    /// One word, or a whole line
    QString text;

    /// 0-based index of the line, in time order
    int line = 0;

    /// The file gave the end; false if it was inferred
    bool endGiven = false;
};

struct Lyrics
{
    /// Sorted by start
    QVector<LyricWord> words;

    /// From [ti:] and [ar:], for the layer's name
    QString title;
    QString artist;

    /// Any <mm:ss.xx> word tags were seen
    bool wordTimed = false;

    bool isEmpty() const { return words.isEmpty(); }

    /// How many lines have words: the lines are numbered from 0 up
    int lineCount() const;

    /**
     * How long a word lasts, at most, when the file does not say
     * where it ends: the next line's start, or the end of the file,
     * would otherwise draw its bar through an instrumental break.
     */
    static constexpr double inferredWordSeconds = 2.0;

    /// How long the last line lasts in a file that times only lines
    static constexpr double inferredLastLineSeconds = 5.0;

    /// A longer word or line is cut to this many characters
    static constexpr int maxLabelLength = 200;

    /// LRC files are a few kB; a bigger file is not read at all
    static constexpr qint64 maxFileBytes = 1024 * 1024;
};

struct LyricsParseResult
{
    Lyrics lyrics;

    /// Non-empty if there is nothing usable (not LRC, no timed lines,
    /// too big); lyrics is then empty
    QString error;

    /// Short sentences on what was skipped or changed, for the status bar
    QStringList warnings;
};

/**
 * Read an LRC file, with line timing ([mm:ss.xx]text) or word timing
 * ([mm:ss.xx]<mm:ss.xx>word <mm:ss.xx>word ...).
 */
LyricsParseResult parseLrc(const QByteArray &bytes);

/**
 * The events of a region model holding the lyrics: frame = start,
 * duration = end - start (at least 1 frame), value = line, label =
 * text.  Sorted as a model holds them, so that the two compare equal.
 */
sv::EventVector lyricsToEvents(const Lyrics &lyrics, sv::sv_samplerate_t rate);

/**
 * The words of those events, back again.  The events keep only the
 * words: title, artist, wordTimed and each endGiven are left empty
 * and false.
 */
Lyrics lyricsFromEvents(const sv::EventVector &events, sv::sv_samplerate_t rate);

#endif
