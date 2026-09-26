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

#ifndef TONY_LYRICS_EDIT_H
#define TONY_LYRICS_EDIT_H

#include "base/BaseTypes.h"
#include "base/Event.h"

#include <QString>

#include <functional>
#include <vector>

/**
 * What editing a word of the lyrics may do: which edge the pointer is
 * on, how far an edge can be dragged, where a new word goes and which
 * line it joins, what a typed text becomes, and how far all the words
 * can be shifted together.
 *
 * The words are the events of the lyrics' region model, in the order
 * the model gives them (by start): a word's start is its frame and its
 * end is frame + duration.  Pure functions over those values, pixel
 * positions and strings: no window and no model, so they are tested
 * without either (TestLyricsEdit).  The editor asks them and does as
 * they say.
 */
namespace LyricsEdit
{
    /// No edit makes a word shorter than this (one already shorter
    /// may stay so, but gets no shorter)
    constexpr double minWordSeconds = 0.020;

    /// How long Add Word makes a word, where there is room for it
    constexpr double newWordSeconds = 0.5;

    /**
     * Seconds in frames at this rate, to the nearest frame, as
     * lyricsToEvents() rounds times; negative seconds give negative
     * frames.  0 for a rate that is not positive.
     */
    sv::sv_frame_t framesFor(double seconds, sv::sv_samplerate_t rate);

    /**
     * The two above in frames at this rate, as framesFor() gives them:
     * 882 and 22050 frames at 44.1 kHz.
     */
    sv::sv_frame_t minWordFrames(sv::sv_samplerate_t rate);
    sv::sv_frame_t newWordFrames(sv::sv_samplerate_t rate);

    /**
     * A word's box in the box row: the pixel columns x0 to x1 - 1 of
     * the view the pointer is in.  A box whose x1 is not past its x0 is
     * painted, and hit, as the one column x0.
     */
    struct Box {
        int x0;
        int x1;
    };
    typedef std::vector<Box> Boxes;

    /**
     * The boxes of the words, in their order: from the x of the word's
     * start to the x of its end, as the lyrics layer paints them.
     * xForFrame is the view's getXForFrame().
     */
    Boxes boxesFor(const sv::EventVector &words,
                   const std::function<int(sv::sv_frame_t)> &xForFrame);

    /**
     * The word whose box holds the column x, or -1 for none.  Where
     * boxes overlap, the one that starts latest, and of those starting
     * in one column the last in order: the one painted on top.
     */
    int wordAt(const Boxes &boxes, int x);

    enum class Part {
        Nothing,    ///< Not on a word nor near an edge
        Start,      ///< On the word's start edge
        End,        ///< On the word's end edge
        Inside      ///< In the word's box, away from its edges
    };

    struct Hit {
        /// An index into the boxes, and so into the words they were
        /// made from; -1 with Nothing
        int word = -1;
        Part part = Part::Nothing;

        bool isEdge() const { return part == Part::Start || part == Part::End; }
    };

    /**
     * What the pointer at column x is on, with grab columns on each
     * side of an edge.
     *
     * An edge is the line between two columns: a start is the line to
     * the left of column x0, an end the line to the right of column
     * x1 - 1.  The pointer is on an edge if it is within grab columns
     * of it, on either side:
     *
     * - In a box (wordAt()): its start if x - x0 < grab, its end if
     *   (x1 - 1) - x < grab, the nearer of the two if both (a narrow
     *   box), and the end if they are equally near; else Inside.
     * - In no box: the end of the box to the left, if x - x1 < grab for
     *   the latest x1 at or before x; the start of the box to the
     *   right, if (x0 - 1) - x < grab for the earliest x0 after x; the
     *   nearer if both, and the end if they are equally near; else
     *   Nothing.  Where several boxes have their edge there, the word
     *   is the one wordAt() gives in the column just inside it.
     *
     * So at an edge two words share, the pointer's side of it picks
     * the word: the column to its left is the earlier word's end, the
     * column to its right the later word's start.  A grab of 0 finds
     * no edges at all.
     */
    Hit hitTest(const Boxes &boxes, int x, int grab);

    /**
     * Where the start of words[word] goes when it is dragged to
     * wanted.  Pass the words as they were when the drag began, and
     * the same ones at every move: the limits come from where the word
     * was then, so a drag back puts it back.
     *
     * - Not before the end of a word that ends at or before its start
     *   (the previous word's, when words do not overlap), nor before
     *   frame 0.  Where another word, starting earlier, already
     *   reaches past its start, the start does not move back at all:
     *   an overlap from a file may stay but does not grow.
     * - Not later than 20 ms before its end; a word already shorter
     *   than 20 ms does not get shorter.
     *
     * The answer is always between where the start was and wanted, so
     * a word never jumps, and its length is never negative.
     */
    sv::sv_frame_t clampStart(const sv::EventVector &words, int word,
                              sv::sv_frame_t wanted,
                              sv::sv_samplerate_t rate);

    /**
     * The end, likewise: not past the start of a word that starts at or
     * after its end (the next word's), and not at all where another
     * word, starting before its end, already reaches past it; not
     * earlier than 20 ms after its start, or where it is for a word
     * already shorter.  After the last word there is no limit.
     */
    sv::sv_frame_t clampEnd(const sv::EventVector &words, int word,
                            sv::sv_frame_t wanted,
                            sv::sv_samplerate_t rate);

    /**
     * words[word] with its start dragged to wanted, as clampStart()
     * allows, and its end where it was.  Its text and line are kept.
     */
    sv::Event startDraggedTo(const sv::EventVector &words, int word,
                             sv::sv_frame_t wanted,
                             sv::sv_samplerate_t rate);

    /// words[word] with its end dragged to wanted, as clampEnd() allows
    sv::Event endDraggedTo(const sv::EventVector &words, int word,
                           sv::sv_frame_t wanted,
                           sv::sv_samplerate_t rate);

    struct Span {
        sv::sv_frame_t start = 0;
        sv::sv_frame_t end = 0;

        sv::sv_frame_t duration() const { return end - start; }
    };

    /**
     * Where Add Word puts a word for a click at frame: from the frame,
     * 0.5 s long, or up to the next word if that is nearer, moved back
     * into the gap as far as that makes it longer (at most 0.5 s).
     * The gap runs from the latest end at or before the frame (or
     * frame 0) to the earliest start after it; after the last word it
     * has no end.
     *
     * False, leaving span alone, if there is no room: the frame is in
     * a word (a word holds its start frame but not its end frame, so a
     * click exactly at a word's end is in the gap after it and the new
     * word starts there), before frame 0, or in a gap shorter than
     * 20 ms.
     */
    bool newWordSpan(const sv::EventVector &words, sv::sv_frame_t frame,
                     sv::sv_samplerate_t rate, Span &span);

    /**
     * The line (the event value) of a new word over span: that of the
     * nearer of its neighbours, by the gap between them, and of the
     * word before it if the gaps are equal; 0 with no words.  The word
     * before is the one ending latest at or before the span's start,
     * the word after the one starting earliest at or after its end
     * (the last and first in order of those, if several); a word
     * overlapping the span, which newWordSpan() never gives, is
     * neither.
     */
    float newWordLine(const sv::EventVector &words, const Span &span);

    /**
     * A typed text as a word's label: cleaned as the parsers clean
     * theirs (lyricsLabel(): controls out, tab to space, trimmed, at
     * most Lyrics::maxLabelLength characters).  Empty if nothing is
     * left, which the edit must refuse.
     */
    QString cleanText(const QString &typed);

    /**
     * How far all the words may move together, for a shift of wanted
     * frames (negative: earlier): as far as wanted, except that the
     * word starting earliest does not go back past frame 0.  Words that
     * already start before frame 0, which no parser makes, do not go
     * back at all.  No limit later.  0 with no words.
     *
     * The answer is always between 0 and wanted.
     */
    sv::sv_frame_t clampShift(const sv::EventVector &words,
                              sv::sv_frame_t wanted);

    /**
     * The words, every one moved by clampShift(words, wanted), start
     * and end alike: lengths, texts and lines are kept, and so is their
     * order.
     */
    sv::EventVector shifted(const sv::EventVector &words,
                            sv::sv_frame_t wanted);
}

#endif
