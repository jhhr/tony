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

#include "LyricsEdit.h"

#include "Lyrics.h"

#include <algorithm>
#include <cmath>

using namespace sv;

namespace {

sv_frame_t framesFor(double seconds, sv_samplerate_t rate)
{
    if (rate <= 0) return 0;
    return sv_frame_t(std::llround(seconds * rate));
}

// One past the box's last column.  A word too short to reach the next
// column is still painted one column wide, and can be hit there
int columnAfter(const LyricsEdit::Box &box)
{
    return std::max(box.x1, box.x0 + 1);
}

sv_frame_t endOf(const Event &word)
{
    return word.getFrame() + word.getDuration();
}

bool isWord(const EventVector &words, int word)
{
    return word >= 0 && word < int(words.size());
}

} // namespace

sv_frame_t
LyricsEdit::minWordFrames(sv_samplerate_t rate)
{
    return framesFor(minWordSeconds, rate);
}

sv_frame_t
LyricsEdit::newWordFrames(sv_samplerate_t rate)
{
    return framesFor(newWordSeconds, rate);
}

LyricsEdit::Boxes
LyricsEdit::boxesFor(const EventVector &words,
                     const std::function<int(sv_frame_t)> &xForFrame)
{
    Boxes boxes;
    boxes.reserve(words.size());
    for (const Event &w : words) {
        boxes.push_back({ xForFrame(w.getFrame()), xForFrame(endOf(w)) });
    }
    return boxes;
}

int
LyricsEdit::wordAt(const Boxes &boxes, int x)
{
    int found = -1;
    for (int i = 0; i < int(boxes.size()); ++i) {
        const Box &box = boxes[i];
        if (x < box.x0 || x >= columnAfter(box)) continue;
        // Later boxes are painted over earlier ones
        if (found < 0 || box.x0 >= boxes[found].x0) found = i;
    }
    return found;
}

LyricsEdit::Hit
LyricsEdit::hitTest(const Boxes &boxes, int x, int grab)
{
    Hit hit;

    int inside = wordAt(boxes, x);
    if (inside >= 0) {
        const Box &box = boxes[inside];
        int toStart = x - box.x0;
        int toEnd = (columnAfter(box) - 1) - x;
        bool onStart = (toStart < grab);
        bool onEnd = (toEnd < grab);
        hit.word = inside;
        if (onStart && (!onEnd || toStart < toEnd)) {
            hit.part = Part::Start;
        } else if (onEnd) {
            hit.part = Part::End;
        } else {
            hit.part = Part::Inside;
        }
        return hit;
    }

    // In no box, every box is wholly to one side of the pointer: the
    // edges that face it are the latest end to its left and the
    // earliest start to its right
    bool haveLeft = false, haveRight = false;
    int leftEnd = 0, rightStart = 0;
    for (const Box &box : boxes) {
        int after = columnAfter(box);
        if (after <= x) {
            if (!haveLeft || after > leftEnd) leftEnd = after;
            haveLeft = true;
        } else {
            if (!haveRight || box.x0 < rightStart) rightStart = box.x0;
            haveRight = true;
        }
    }

    int toEnd = x - leftEnd;
    int toStart = (rightStart - 1) - x;
    bool onEnd = (haveLeft && toEnd < grab);
    bool onStart = (haveRight && toStart < grab);

    // The word each edge belongs to is the one a pointer just inside
    // it would find, so an edge is the same word's from either side
    if (onEnd && (!onStart || toEnd <= toStart)) {
        hit.word = wordAt(boxes, leftEnd - 1);
        hit.part = Part::End;
    } else if (onStart) {
        hit.word = wordAt(boxes, rightStart);
        hit.part = Part::Start;
    }
    return hit;
}

sv_frame_t
LyricsEdit::clampStart(const EventVector &words, int word,
                       sv_frame_t wanted, sv_samplerate_t rate)
{
    if (!isWord(words, word)) return wanted;

    const sv_frame_t start = words[word].getFrame();
    const sv_frame_t end = endOf(words[word]);

    // Back to the latest end at or before the start; not back at all
    // where an earlier word already overlaps it, since that would make
    // the overlap longer
    sv_frame_t lowest = 0;
    for (int i = 0; i < int(words.size()); ++i) {
        if (i == word) continue;
        const Event &other = words[i];
        if (endOf(other) <= start) {
            lowest = std::max(lowest, endOf(other));
        } else if (other.getFrame() < start) {
            lowest = start;
        }
    }
    // Every limit is where the start already is or before it, so the
    // word can only move the way it is dragged.  (A start before frame
    // 0, which no parser makes, stays where it is.)
    lowest = std::min(lowest, start);

    // Forward to 20 ms before the end, or nowhere for a word that is
    // already shorter
    const sv_frame_t highest = std::max(start, end - minWordFrames(rate));

    return std::clamp(wanted, lowest, highest);
}

sv_frame_t
LyricsEdit::clampEnd(const EventVector &words, int word,
                     sv_frame_t wanted, sv_samplerate_t rate)
{
    if (!isWord(words, word)) return wanted;

    const sv_frame_t start = words[word].getFrame();
    const sv_frame_t end = endOf(words[word]);

    // Back to 20 ms after the start, or nowhere for a word that is
    // already shorter
    const sv_frame_t lowest = std::min(end, start + minWordFrames(rate));

    // Forward to the earliest start at or after the end; not forward at
    // all where another word already reaches past the end.  Nothing
    // after the last word
    bool limited = false;
    sv_frame_t highest = end;
    for (int i = 0; i < int(words.size()); ++i) {
        if (i == word) continue;
        const Event &other = words[i];
        sv_frame_t limit;
        if (other.getFrame() >= end) {
            limit = other.getFrame();
        } else if (endOf(other) > end) {
            limit = end;
        } else {
            continue;
        }
        if (!limited || limit < highest) highest = limit;
        limited = true;
    }

    if (wanted < lowest) return lowest;
    if (limited && wanted > highest) return highest;
    return wanted;
}

Event
LyricsEdit::startDraggedTo(const EventVector &words, int word,
                           sv_frame_t wanted, sv_samplerate_t rate)
{
    if (!isWord(words, word)) return Event();
    const Event &w = words[word];
    sv_frame_t start = clampStart(words, word, wanted, rate);
    return w.withFrame(start).withDuration(endOf(w) - start);
}

Event
LyricsEdit::endDraggedTo(const EventVector &words, int word,
                         sv_frame_t wanted, sv_samplerate_t rate)
{
    if (!isWord(words, word)) return Event();
    const Event &w = words[word];
    sv_frame_t end = clampEnd(words, word, wanted, rate);
    return w.withDuration(end - w.getFrame());
}

bool
LyricsEdit::newWordSpan(const EventVector &words, sv_frame_t frame,
                        sv_samplerate_t rate, Span &span)
{
    if (rate <= 0 || frame < 0) return false;

    // The gap the frame is in, if it is in one
    sv_frame_t gapStart = 0;
    bool haveGapEnd = false;
    sv_frame_t gapEnd = 0;
    for (const Event &w : words) {
        if (endOf(w) <= frame) {
            gapStart = std::max(gapStart, endOf(w));
        } else if (w.getFrame() <= frame) {
            return false;
        } else if (!haveGapEnd || w.getFrame() < gapEnd) {
            gapEnd = w.getFrame();
            haveGapEnd = true;
        }
    }

    sv_frame_t length = newWordFrames(rate);
    sv_frame_t start = frame;

    if (haveGapEnd) {
        sv_frame_t room = gapEnd - gapStart;
        if (room < minWordFrames(rate)) return false;
        // Up to the next word, and back from the click into the gap
        // as far as that makes the word longer
        length = std::min(length, room);
        start = std::min(frame, gapEnd - length);
    }

    span.start = start;
    span.end = start + length;
    return true;
}

float
LyricsEdit::newWordLine(const EventVector &words, const Span &span)
{
    int before = -1, after = -1;
    for (int i = 0; i < int(words.size()); ++i) {
        const Event &w = words[i];
        if (endOf(w) <= span.start) {
            if (before < 0 || endOf(w) >= endOf(words[before])) before = i;
        } else if (w.getFrame() >= span.end) {
            if (after < 0 || w.getFrame() < words[after].getFrame()) after = i;
        }
    }

    if (before < 0 && after < 0) return 0.f;
    if (after < 0) return words[before].getValue();
    if (before < 0) return words[after].getValue();

    // A word more often continues the line it follows than starts one
    sv_frame_t gapBefore = span.start - endOf(words[before]);
    sv_frame_t gapAfter = words[after].getFrame() - span.end;
    return (gapBefore <= gapAfter ?
            words[before].getValue() : words[after].getValue());
}

QString
LyricsEdit::cleanText(const QString &typed)
{
    return lyricsLabel(typed);
}
