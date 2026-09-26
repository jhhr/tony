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

#ifndef TONY_LYRICS_TTML_H
#define TONY_LYRICS_TTML_H

#include "Lyrics.h"

#include <QByteArray>

/**
 * Timed lyrics in TTML, as Apple Music has them and as the
 * Moises-Lyric-Exporter and AMLL TTML Tool write them: a <p> per line
 * and a <span begin end> per word.  Pure functions, as in Lyrics.h
 * (TestLyricsTtml).
 */

/**
 * Read a TTML file.  Its times are read as absolute, as every lyrics
 * tool writes them, although strict TTML makes a child's times
 * relative to its parent's.  Timed spans with no whitespace between
 * them are syllables of one word, and are joined; background vocals,
 * translations and romanisations are skipped; a <p> with no timed
 * spans is one word, the whole line.  A file with a <!DOCTYPE> is
 * refused: TTML has none, and it is how entity tricks get in.
 */
LyricsParseResult parseTtml(const QByteArray &bytes);

/**
 * The lyrics as TTML, UTF-8, in the exporter's Apple style: one agent,
 * a <p> per line, a <span> per word, times m:ss.mmm rounded to the
 * millisecond.  The title, if any, goes in <ttm:title>; the artist is
 * not written.  parseTtml() gives the same words back, to half a
 * millisecond.
 */
QByteArray writeTtml(const Lyrics &lyrics);

#endif
