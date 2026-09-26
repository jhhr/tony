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

#ifndef TEST_LYRICS_EDIT_H
#define TEST_LYRICS_EDIT_H

// Tier 2: what an edit of the lyrics may do, as numbers: which edge
// the pointer is on, how far a dragged edge goes, where a new word
// goes and which line it joins, and what typed text becomes. No window
// and no model: what the editor does with the answers is the app
// suite's business.

#include "../LyricsEdit.h"
#include "../Lyrics.h"

#include <QObject>
#include <QtTest>

#include <algorithm>

class TestLyricsEdit : public QObject
{
    Q_OBJECT

    typedef sv::sv_frame_t frame_t;
    typedef LyricsEdit::Boxes Boxes;
    typedef LyricsEdit::Span Span;

    static constexpr double kRate = 44100.0;

    // 20 ms and 0.5 s at that rate
    static constexpr frame_t kMin = 882;
    static constexpr frame_t kNew = 22050;

    static constexpr frame_t kSecond = 44100;

    // The grab width the editor is meant to use, about 6 pixels
    static constexpr int kGrab = 6;

    static sv::Event word(frame_t start, frame_t end, float line = 0.f,
                          const char *text = "word") {
        return sv::Event(start, line, end - start, QString(text));
    }

    static QString describe(const sv::Event &e) {
        return QString("\"%1\" %2-%3 line %4")
            .arg(e.getLabel()).arg(e.getFrame())
            .arg(e.getFrame() + e.getDuration()).arg(e.getValue());
    }

    static QString describe(const LyricsEdit::Hit &hit) {
        switch (hit.part) {
        case LyricsEdit::Part::Nothing: return QString("nothing %1").arg(hit.word);
        case LyricsEdit::Part::Start: return QString("start of %1").arg(hit.word);
        case LyricsEdit::Part::End: return QString("end of %1").arg(hit.word);
        case LyricsEdit::Part::Inside: return QString("inside %1").arg(hit.word);
        }
        return QString("?");
    }

    // What the pointer at x is on
    static QString at(const Boxes &boxes, int x, int grab = kGrab) {
        return describe(LyricsEdit::hitTest(boxes, x, grab));
    }

    // Where Add Word puts a word for a click at frame
    static QString newSpan(const sv::EventVector &words, frame_t frame) {
        Span s;
        if (!LyricsEdit::newWordSpan(words, frame, kRate, s)) return "none";
        return QString("%1-%2").arg(s.start).arg(s.end);
    }

    // The same, as expected
    static QString span(frame_t start, frame_t end) {
        return QString("%1-%2").arg(start).arg(end);
    }

    static frame_t startTo(const sv::EventVector &words, int i, frame_t f) {
        return LyricsEdit::clampStart(words, i, f, kRate);
    }

    static frame_t endTo(const sv::EventVector &words, int i, frame_t f) {
        return LyricsEdit::clampEnd(words, i, f, kRate);
    }

    static frame_t endOf(const sv::Event &e) {
        return e.getFrame() + e.getDuration();
    }

    static frame_t overlap(frame_t s0, frame_t e0, frame_t s1, frame_t e1) {
        return std::max(frame_t(0), std::min(e0, e1) - std::max(s0, s1));
    }

private slots:
    // 20 ms and 0.5 s to the nearest frame: whole numbers at the usual
    // rates, whatever 0.02 is in binary
    void limits_in_frames() {
        QCOMPARE(LyricsEdit::minWordSeconds, 0.020);
        QCOMPARE(LyricsEdit::newWordSeconds, 0.5);
        QCOMPARE(LyricsEdit::minWordFrames(44100), kMin);
        QCOMPARE(LyricsEdit::minWordFrames(48000), frame_t(960));
        QCOMPARE(LyricsEdit::minWordFrames(22050), frame_t(441));
        QCOMPARE(LyricsEdit::minWordFrames(96000), frame_t(1920));
        QCOMPARE(LyricsEdit::newWordFrames(44100), kNew);
        QCOMPARE(LyricsEdit::newWordFrames(48000), frame_t(24000));
        QCOMPARE(LyricsEdit::minWordFrames(0), frame_t(0));
        QCOMPARE(LyricsEdit::newWordFrames(0), frame_t(0));
    }

    // A box runs from the x of the word's start to the x of its end
    void boxes_from_the_word_times() {
        sv::EventVector words { word(1000, 2500), word(2500, 2560),
                                word(5000, 5000) };
        Boxes boxes = LyricsEdit::boxesFor
            (words, [](frame_t f) { return int(f / 100) + 7; });
        QCOMPARE(int(boxes.size()), 3);
        QCOMPARE(boxes[0].x0, 17);
        QCOMPARE(boxes[0].x1, 32);
        QCOMPARE(boxes[1].x0, 32);
        QCOMPARE(boxes[1].x1, 32);
        QCOMPARE(boxes[2].x0, 57);
        QCOMPARE(boxes[2].x1, 57);
        QVERIFY(LyricsEdit::boxesFor({}, [](frame_t) { return 0; }).empty());
    }

    // A box holds its columns x0 to x1 - 1, and one too short to reach
    // a column is painted, and found, in the column x0
    void word_at_a_column() {
        Boxes boxes { { 100, 150 }, { 150, 200 }, { 220, 221 }, { 300, 300 } };
        QCOMPARE(LyricsEdit::wordAt(boxes, 99), -1);
        QCOMPARE(LyricsEdit::wordAt(boxes, 100), 0);
        QCOMPARE(LyricsEdit::wordAt(boxes, 149), 0);
        QCOMPARE(LyricsEdit::wordAt(boxes, 150), 1);
        QCOMPARE(LyricsEdit::wordAt(boxes, 199), 1);
        QCOMPARE(LyricsEdit::wordAt(boxes, 200), -1);
        QCOMPARE(LyricsEdit::wordAt(boxes, 220), 2);
        QCOMPARE(LyricsEdit::wordAt(boxes, 221), -1);
        QCOMPARE(LyricsEdit::wordAt(boxes, 299), -1);
        QCOMPARE(LyricsEdit::wordAt(boxes, 300), 3);
        QCOMPARE(LyricsEdit::wordAt(boxes, 301), -1);
        QCOMPARE(LyricsEdit::wordAt({}, 100), -1);

        // Overlapping: the one that starts latest, painted on top
        Boxes crossing { { 100, 200 }, { 150, 250 } };
        QCOMPARE(LyricsEdit::wordAt(crossing, 149), 0);
        QCOMPARE(LyricsEdit::wordAt(crossing, 150), 1);
        QCOMPARE(LyricsEdit::wordAt(crossing, 199), 1);
        QCOMPARE(LyricsEdit::wordAt(crossing, 249), 1);

        Boxes within { { 100, 300 }, { 150, 200 } };
        QCOMPARE(LyricsEdit::wordAt(within, 149), 0);
        QCOMPARE(LyricsEdit::wordAt(within, 150), 1);
        QCOMPARE(LyricsEdit::wordAt(within, 199), 1);
        QCOMPARE(LyricsEdit::wordAt(within, 200), 0);

        // Starting in one column: the later one in order
        Boxes together { { 100, 101 }, { 100, 200 } };
        QCOMPARE(LyricsEdit::wordAt(together, 100), 1);
        Boxes shortLast { { 100, 150 }, { 100, 100 } };
        QCOMPARE(LyricsEdit::wordAt(shortLast, 100), 1);
        QCOMPARE(LyricsEdit::wordAt(shortLast, 101), 0);
    }

    // Decision 3: at an edge two words share, the column to its left is
    // the earlier word's end, the column to its right the later word's
    // start, each for grab columns
    void shared_edge_from_each_side() {
        Boxes boxes { { 100, 150 }, { 150, 200 } };
        QCOMPARE(at(boxes, 149), QString("end of 0"));
        QCOMPARE(at(boxes, 144), QString("end of 0"));
        QCOMPARE(at(boxes, 143), QString("inside 0"));
        QCOMPARE(at(boxes, 150), QString("start of 1"));
        QCOMPARE(at(boxes, 155), QString("start of 1"));
        QCOMPARE(at(boxes, 156), QString("inside 1"));

        // The outer edges, from inside and from outside
        QCOMPARE(at(boxes, 100), QString("start of 0"));
        QCOMPARE(at(boxes, 105), QString("start of 0"));
        QCOMPARE(at(boxes, 106), QString("inside 0"));
        QCOMPARE(at(boxes, 99), QString("start of 0"));
        QCOMPARE(at(boxes, 94), QString("start of 0"));
        QCOMPARE(at(boxes, 93), QString("nothing -1"));
        QCOMPARE(at(boxes, 199), QString("end of 1"));
        QCOMPARE(at(boxes, 194), QString("end of 1"));
        QCOMPARE(at(boxes, 193), QString("inside 1"));
        QCOMPARE(at(boxes, 200), QString("end of 1"));
        QCOMPARE(at(boxes, 205), QString("end of 1"));
        QCOMPARE(at(boxes, 206), QString("nothing -1"));

        // With a grab of one column, only the columns touching the edge
        QCOMPARE(at(boxes, 149, 1), QString("end of 0"));
        QCOMPARE(at(boxes, 150, 1), QString("start of 1"));
        QCOMPARE(at(boxes, 148, 1), QString("inside 0"));
        QCOMPARE(at(boxes, 151, 1), QString("inside 1"));
    }

    // In a box too narrow for both edges' grab columns, the nearer edge,
    // and the end where they are equally near
    void narrow_boxes() {
        Boxes four { { 100, 104 } };
        QCOMPARE(at(four, 100), QString("start of 0"));
        QCOMPARE(at(four, 101), QString("start of 0"));
        QCOMPARE(at(four, 102), QString("end of 0"));
        QCOMPARE(at(four, 103), QString("end of 0"));

        Boxes three { { 100, 103 } };
        QCOMPARE(at(three, 100), QString("start of 0"));
        QCOMPARE(at(three, 101), QString("end of 0"));
        QCOMPARE(at(three, 102), QString("end of 0"));

        Boxes two { { 100, 102 } };
        QCOMPARE(at(two, 100), QString("start of 0"));
        QCOMPARE(at(two, 101), QString("end of 0"));

        // One column wide: its end inside, and each edge from its side
        Boxes one { { 100, 101 } };
        QCOMPARE(at(one, 100), QString("end of 0"));
        QCOMPARE(at(one, 99), QString("start of 0"));
        QCOMPARE(at(one, 94), QString("start of 0"));
        QCOMPARE(at(one, 93), QString("nothing -1"));
        QCOMPARE(at(one, 101), QString("end of 0"));
        QCOMPARE(at(one, 106), QString("end of 0"));
        QCOMPARE(at(one, 107), QString("nothing -1"));
        QCOMPARE(at(one, 100, 1), QString("end of 0"));
        QCOMPARE(at(one, 99, 1), QString("start of 0"));
        QCOMPARE(at(one, 101, 1), QString("end of 0"));
        QCOMPARE(at(one, 98, 1), QString("nothing -1"));
        QCOMPARE(at(one, 102, 1), QString("nothing -1"));

        // No width at all is painted one column wide, and is the same
        Boxes none { { 100, 100 } };
        QCOMPARE(at(none, 100), QString("end of 0"));
        QCOMPARE(at(none, 99), QString("start of 0"));
        QCOMPARE(at(none, 101), QString("end of 0"));

        // A one-column word between two others: its start is under the
        // earlier word's end, as painted
        Boxes squeezed { { 50, 100 }, { 100, 101 }, { 101, 150 } };
        QCOMPARE(at(squeezed, 99), QString("end of 0"));
        QCOMPARE(at(squeezed, 100), QString("end of 1"));
        QCOMPARE(at(squeezed, 101), QString("start of 2"));
    }

    // Overlapping words from a file: the box that starts latest is the
    // one the pointer is in, and its edges are the ones found
    void overlapping_boxes() {
        Boxes crossing { { 100, 200 }, { 150, 250 } };
        QCOMPARE(at(crossing, 101), QString("start of 0"));
        QCOMPARE(at(crossing, 149), QString("inside 0"));
        QCOMPARE(at(crossing, 150), QString("start of 1"));
        QCOMPARE(at(crossing, 155), QString("start of 1"));
        QCOMPARE(at(crossing, 160), QString("inside 1"));
        // The earlier word's end is under the later word
        QCOMPARE(at(crossing, 199), QString("inside 1"));
        QCOMPARE(at(crossing, 200), QString("inside 1"));
        QCOMPARE(at(crossing, 249), QString("end of 1"));
        QCOMPARE(at(crossing, 252), QString("end of 1"));

        Boxes within { { 100, 300 }, { 150, 200 } };
        QCOMPARE(at(within, 150), QString("start of 1"));
        QCOMPARE(at(within, 199), QString("end of 1"));
        QCOMPARE(at(within, 200), QString("inside 0"));
        QCOMPARE(at(within, 298), QString("end of 0"));

        // Two starting in one column, or ending in one: the later word
        // in order, from inside and from outside alike
        Boxes together { { 100, 101 }, { 100, 200 } };
        QCOMPARE(at(together, 100), QString("start of 1"));
        QCOMPARE(at(together, 99), QString("start of 1"));
        Boxes endTogether { { 100, 200 }, { 150, 200 } };
        QCOMPARE(at(endTogether, 199), QString("end of 1"));
        QCOMPARE(at(endTogether, 200), QString("end of 1"));
    }

    // Between boxes, the nearest edge within grab columns, and the end
    // where the two are equally near
    void gap_between_boxes() {
        Boxes wide { { 100, 150 }, { 170, 200 } };
        QCOMPARE(at(wide, 150), QString("end of 0"));
        QCOMPARE(at(wide, 155), QString("end of 0"));
        QCOMPARE(at(wide, 156), QString("nothing -1"));
        QCOMPARE(at(wide, 163), QString("nothing -1"));
        QCOMPARE(at(wide, 164), QString("start of 1"));
        QCOMPARE(at(wide, 169), QString("start of 1"));

        // An odd gap: its middle column is as near to both
        Boxes odd { { 100, 150 }, { 155, 200 } };
        QCOMPARE(at(odd, 150), QString("end of 0"));
        QCOMPARE(at(odd, 151), QString("end of 0"));
        QCOMPARE(at(odd, 152), QString("end of 0"));
        QCOMPARE(at(odd, 153), QString("start of 1"));
        QCOMPARE(at(odd, 154), QString("start of 1"));

        Boxes even { { 100, 150 }, { 154, 200 } };
        QCOMPARE(at(even, 150), QString("end of 0"));
        QCOMPARE(at(even, 151), QString("end of 0"));
        QCOMPARE(at(even, 152), QString("start of 1"));
        QCOMPARE(at(even, 153), QString("start of 1"));
    }

    void nothing_near() {
        QCOMPARE(at({}, 0), QString("nothing -1"));
        QCOMPARE(at({}, 100), QString("nothing -1"));

        Boxes boxes { { 100, 150 }, { 150, 200 } };
        QCOMPARE(at(boxes, 0), QString("nothing -1"));
        QCOMPARE(at(boxes, -50), QString("nothing -1"));
        QCOMPARE(at(boxes, 1000), QString("nothing -1"));

        // No grab, no edges
        QCOMPARE(at(boxes, 100, 0), QString("inside 0"));
        QCOMPARE(at(boxes, 149, 0), QString("inside 0"));
        QCOMPARE(at(boxes, 150, 0), QString("inside 1"));
        QCOMPARE(at(boxes, 99, 0), QString("nothing -1"));
        QCOMPARE(at(boxes, 200, 0), QString("nothing -1"));
    }

    // Words that do not overlap: a start between the previous word's end
    // (or frame 0) and 20 ms before its own end; an end between 20 ms
    // after its start and the next word's start, or anywhere after the
    // last word
    void clamps_between_neighbours() {
        sv::EventVector words { word(0, 10000), word(12000, 20000),
                                word(20000, 30000) };

        QCOMPARE(startTo(words, 1, 11000), frame_t(11000));
        QCOMPARE(startTo(words, 1, 12000), frame_t(12000));
        QCOMPARE(startTo(words, 1, 10000), frame_t(10000));
        QCOMPARE(startTo(words, 1, 9999), frame_t(10000));
        QCOMPARE(startTo(words, 1, -500), frame_t(10000));
        QCOMPARE(startTo(words, 1, 20000 - kMin), 20000 - kMin);
        QCOMPARE(startTo(words, 1, 20000 - kMin + 1), 20000 - kMin);
        QCOMPARE(startTo(words, 1, 25000), 20000 - kMin);

        // Touching the word before: no further back at all
        QCOMPARE(startTo(words, 2, 19000), frame_t(20000));
        QCOMPARE(startTo(words, 2, 29500), 30000 - kMin);

        // The first word: not before frame 0
        QCOMPARE(startTo(words, 0, -100), frame_t(0));
        QCOMPARE(startTo(words, 0, 5000), frame_t(5000));
        QCOMPARE(startTo(words, 0, 9500), 10000 - kMin);
        sv::EventVector later { word(5000, 8000) };
        QCOMPARE(startTo(later, 0, 100), frame_t(100));
        QCOMPARE(startTo(later, 0, -100), frame_t(0));

        QCOMPARE(endTo(words, 0, 11000), frame_t(11000));
        QCOMPARE(endTo(words, 0, 12000), frame_t(12000));
        QCOMPARE(endTo(words, 0, 12001), frame_t(12000));
        QCOMPARE(endTo(words, 0, 13000), frame_t(12000));
        QCOMPARE(endTo(words, 0, kMin), kMin);
        QCOMPARE(endTo(words, 0, kMin - 1), kMin);
        QCOMPARE(endTo(words, 0, -500), kMin);

        // Touching the word after: no further forward at all
        QCOMPARE(endTo(words, 1, 21000), frame_t(20000));
        QCOMPARE(endTo(words, 1, 19000), frame_t(19000));
        QCOMPARE(endTo(words, 1, 12500), 12000 + kMin);

        // The last word: no limit forward
        QCOMPARE(endTo(words, 2, 1000000), frame_t(1000000));
        QCOMPARE(endTo(words, 2, 20100), 20000 + kMin);
    }

    // The dragged word keeps its other edge, its text and its line
    void dragged_word_keeps_the_rest() {
        sv::EventVector words { word(0, 10000, 2.f, "hello"),
                                word(12000, 20000, 2.f, "world") };

        QCOMPARE(describe(LyricsEdit::startDraggedTo(words, 1, 11000, kRate)),
                 describe(word(11000, 20000, 2.f, "world")));
        QCOMPARE(describe(LyricsEdit::startDraggedTo(words, 1, 9000, kRate)),
                 describe(word(10000, 20000, 2.f, "world")));
        QCOMPARE(describe(LyricsEdit::startDraggedTo(words, 1, 19900, kRate)),
                 describe(word(20000 - kMin, 20000, 2.f, "world")));
        QCOMPARE(describe(LyricsEdit::endDraggedTo(words, 0, 11000, kRate)),
                 describe(word(0, 11000, 2.f, "hello")));
        QCOMPARE(describe(LyricsEdit::endDraggedTo(words, 0, 13000, kRate)),
                 describe(word(0, 12000, 2.f, "hello")));
        QCOMPARE(describe(LyricsEdit::endDraggedTo(words, 1, 50000, kRate)),
                 describe(word(12000, 50000, 2.f, "world")));

        // Not dragged anywhere: the same event, which a command folds
        // away
        QVERIFY(LyricsEdit::startDraggedTo(words, 1, 12000, kRate) == words[1]);
        QVERIFY(LyricsEdit::endDraggedTo(words, 1, 20000, kRate) == words[1]);

        // No such word: nothing to clamp, and no crash
        QCOMPARE(startTo(words, 2, 500), frame_t(500));
        QCOMPARE(endTo(words, -1, 500), frame_t(500));
    }

    // Decision 6: an overlap already in a file may stay, and shrink, but
    // never grows; the limits come from the words as they were when the
    // drag began, so a drag back puts the word back
    void clamps_with_an_overlap_already_there() {
        sv::EventVector crossing { word(0, 15000), word(12000, 20000),
                                   word(25000, 30000) };
        QCOMPARE(startTo(crossing, 1, 11000), frame_t(12000));
        QCOMPARE(startTo(crossing, 1, 13000), frame_t(13000));
        QCOMPARE(startTo(crossing, 1, 16000), frame_t(16000));
        QCOMPARE(endTo(crossing, 0, 16000), frame_t(15000));
        QCOMPARE(endTo(crossing, 0, 14000), frame_t(14000));
        QCOMPARE(endTo(crossing, 0, 11000), frame_t(11000));
        QCOMPARE(endTo(crossing, 1, 22000), frame_t(22000));
        QCOMPARE(endTo(crossing, 1, 26000), frame_t(25000));

        // One word inside another: the inner one only shrinks, the
        // outer one's end is not held by it
        sv::EventVector within { word(0, 30000), word(10000, 12000),
                                 word(40000, 50000) };
        QCOMPARE(startTo(within, 1, 5000), frame_t(10000));
        QCOMPARE(startTo(within, 1, 11500), 12000 - kMin);
        QCOMPARE(endTo(within, 1, 20000), frame_t(12000));
        QCOMPARE(endTo(within, 1, 11000), frame_t(11000));
        QCOMPARE(endTo(within, 0, 35000), frame_t(35000));
        QCOMPARE(endTo(within, 0, 45000), frame_t(40000));
        QCOMPARE(endTo(within, 0, 11000), frame_t(11000));

        // Two words at one frame, as an LRC file gives them: the first
        // one frame long.  A word starting at the same frame holds
        // neither start back, but it does hold the short one's end
        sv::EventVector together { word(0, 5000), word(10000, 10001),
                                   word(10000, 20000) };
        QCOMPARE(startTo(together, 2, 7000), frame_t(7000));
        QCOMPARE(startTo(together, 2, 4000), frame_t(5000));
        QCOMPARE(startTo(together, 1, 3000), frame_t(5000));
        QCOMPARE(endTo(together, 1, 15000), frame_t(10001));
        QCOMPARE(endTo(together, 2, 25000), frame_t(25000));

        // The same word twice: each is free of the other
        sv::EventVector twice { word(10000, 20000), word(10000, 20000) };
        QCOMPARE(startTo(twice, 0, 5000), frame_t(5000));
        QCOMPARE(endTo(twice, 1, 25000), frame_t(25000));
    }

    // A word already shorter than 20 ms does not get shorter, but can
    // be made longer; once it is, the 20 ms hold
    void clamps_of_a_word_under_20ms() {
        sv::EventVector words { word(0, 9000), word(10000, 10400),
                                word(11000, 20000) };
        QCOMPARE(startTo(words, 1, 10200), frame_t(10000));
        QCOMPARE(startTo(words, 1, 12000), frame_t(10000));
        QCOMPARE(endTo(words, 1, 10200), frame_t(10400));
        QCOMPARE(endTo(words, 1, 0), frame_t(10400));
        QCOMPARE(startTo(words, 1, 9500), frame_t(9500));
        QCOMPARE(startTo(words, 1, 8000), frame_t(9000));
        QCOMPARE(endTo(words, 1, 10800), frame_t(10800));
        QCOMPARE(endTo(words, 1, 12000), frame_t(11000));

        sv::EventVector longer { word(0, 9000), word(9500, 10800),
                                 word(11000, 20000) };
        QCOMPARE(startTo(longer, 1, 10500), 10800 - kMin);

        // A word of no length (never made by the parsers) stays at no
        // length, not less
        sv::EventVector none { word(0, 9000), word(10000, 10000),
                               word(11000, 20000) };
        QCOMPARE(startTo(none, 1, 10500), frame_t(10000));
        QCOMPARE(endTo(none, 1, 9500), frame_t(10000));
        QCOMPARE(startTo(none, 1, 9500), frame_t(9500));
        QCOMPARE(endTo(none, 1, 10500), frame_t(10500));
    }

    // Whatever the words and wherever the pointer goes: the edge ends up
    // between where it was and where it was dragged, the word is never
    // shorter than 20 ms or than it was, and no overlap grows
    void no_drag_jumps_or_grows_an_overlap() {
        const std::vector<sv::EventVector> cases {
            { word(0, 10000), word(10000, 20000), word(20000, 30000) },
            { word(1000, 5000), word(8000, 9000), word(15000, 40000) },
            { word(0, 15000), word(12000, 20000), word(25000, 30000) },
            { word(0, 30000), word(10000, 12000), word(40000, 50000) },
            { word(0, 5000), word(10000, 10001), word(10000, 20000) },
            { word(10000, 20000), word(10000, 20000) },
            { word(0, 9000), word(10000, 10400), word(11000, 20000) },
            { word(0, 9000), word(10000, 10000), word(11000, 20000) },
            { word(0, 20000), word(5000, 25000), word(8000, 12000),
              word(30000, 30500) },
        };

        const frame_t minimum = LyricsEdit::minWordFrames(kRate);

        for (int c = 0; c < int(cases.size()); ++c) {
            const sv::EventVector &words = cases[c];

            std::vector<frame_t> targets;
            for (frame_t f = -2000; f < 55000; f += 97) targets.push_back(f);
            for (const sv::Event &e : words) {
                for (frame_t f : { e.getFrame(), endOf(e) }) {
                    for (frame_t d : { frame_t(-1), frame_t(0), frame_t(1),
                                       -kMin, kMin }) {
                        targets.push_back(f + d);
                    }
                }
            }

            for (int i = 0; i < int(words.size()); ++i) {
                const frame_t s = words[i].getFrame();
                const frame_t e = endOf(words[i]);
                const frame_t shortest =
                    std::min(e - s, minimum);

                for (frame_t wanted : targets) {
                    for (bool start : { true, false }) {
                        sv::Event moved = start ?
                            LyricsEdit::startDraggedTo(words, i, wanted, kRate) :
                            LyricsEdit::endDraggedTo(words, i, wanted, kRate);
                        const frame_t ms = moved.getFrame();
                        const frame_t me = endOf(moved);
                        const frame_t edge = start ? ms : me;
                        const frame_t was = start ? s : e;
                        const frame_t other = start ? me : ms;
                        const frame_t otherWas = start ? e : s;

                        const QString where =
                            QString("case %1, word %2, %3 to %4: %5-%6")
                            .arg(c).arg(i).arg(start ? "start" : "end")
                            .arg(wanted).arg(ms).arg(me);

                        QVERIFY2(other == otherWas, qPrintable(where));
                        QVERIFY2(edge >= std::min(was, wanted) &&
                                 edge <= std::max(was, wanted),
                                 qPrintable(where));
                        QVERIFY2(moved.getDuration() >= shortest,
                                 qPrintable(where));
                        QVERIFY2(ms >= 0, qPrintable(where));
                        for (int j = 0; j < int(words.size()); ++j) {
                            if (j == i) continue;
                            const frame_t js = words[j].getFrame();
                            const frame_t je = endOf(words[j]);
                            QVERIFY2(overlap(ms, me, js, je) <=
                                     overlap(s, e, js, je),
                                     qPrintable(where + QString(", word %1")
                                                .arg(j)));
                        }
                    }
                }
            }
        }
    }

    // Decision 9: 0.5 s from the click where there is room, up to the
    // next word if that is nearer, moved back into the gap as far as
    // that makes it longer
    void new_word_where_there_is_room() {
        const frame_t S = kSecond;
        sv::EventVector words { word(0, S), word(3 * S, 4 * S) };

        QCOMPARE(newSpan(words, S + S / 2), span(S + S / 2, S + S / 2 + kNew));
        QCOMPARE(newSpan(words, 2 * S), span(2 * S, 2 * S + kNew));

        // At exactly a word's end: the gap after it begins there
        QCOMPARE(newSpan(words, S), span(S, S + kNew));

        // Near the next word: 0.5 s ending at it
        QCOMPARE(newSpan(words, 3 * S - 8820), span(3 * S - kNew, 3 * S));
        QCOMPARE(newSpan(words, 3 * S - 1), span(3 * S - kNew, 3 * S));
        QCOMPARE(newSpan(words, 3 * S - kNew), span(3 * S - kNew, 3 * S));
        QCOMPARE(newSpan(words, 3 * S - kNew - 1), span(3 * S - kNew - 1, 3 * S - 1));
    }

    void new_word_with_little_room() {
        const frame_t S = kSecond;

        // A gap shorter than 0.5 s: the whole of it, wherever the click
        sv::EventVector words { word(0, S), word(S + 10000, 2 * S) };
        QCOMPARE(newSpan(words, S), span(S, S + 10000));
        QCOMPARE(newSpan(words, S + 5000), span(S, S + 10000));
        QCOMPARE(newSpan(words, S + 9999), span(S, S + 10000));

        // 20 ms is room enough; less is not
        sv::EventVector just { word(0, S), word(S + kMin, 2 * S) };
        QCOMPARE(newSpan(just, S + 400), span(S, S + kMin));
        sv::EventVector under { word(0, S), word(S + kMin - 1, 2 * S) };
        QCOMPARE(newSpan(under, S), QString("none"));
        QCOMPARE(newSpan(under, S + 400), QString("none"));
    }

    // Add Word is for empty space: none in a word, before frame 0, or
    // without a sample rate
    void new_word_none_in_a_word() {
        const frame_t S = kSecond;
        sv::EventVector words { word(0, S), word(3 * S, 4 * S) };
        QCOMPARE(newSpan(words, 0), QString("none"));
        QCOMPARE(newSpan(words, S / 2), QString("none"));
        QCOMPARE(newSpan(words, S - 1), QString("none"));
        QCOMPARE(newSpan(words, 3 * S), QString("none"));
        QCOMPARE(newSpan(words, 4 * S - 1), QString("none"));
        QCOMPARE(newSpan({}, -1), QString("none"));

        Span s;
        s.start = 7;
        s.end = 9;
        QVERIFY(!LyricsEdit::newWordSpan(words, 2 * S, 0, s));
        QVERIFY(!LyricsEdit::newWordSpan(words, S / 2, kRate, s));
        QCOMPARE(s.start, frame_t(7));
        QCOMPARE(s.end, frame_t(9));
    }

    // Before the first word the gap starts at frame 0; after the last
    // it has no end
    void new_word_before_the_first_and_after_the_last() {
        const frame_t S = kSecond;
        sv::EventVector words { word(3 * S, 4 * S) };
        QCOMPARE(newSpan(words, 0), span(0, kNew));
        QCOMPARE(newSpan(words, S), span(S, S + kNew));
        QCOMPARE(newSpan(words, 4 * S), span(4 * S, 4 * S + kNew));
        QCOMPARE(newSpan(words, 10 * S), span(10 * S, 10 * S + kNew));

        // A first word 0.3 s in: the new one fills the 0.3 s before it
        sv::EventVector early { word(13230, S) };
        QCOMPARE(newSpan(early, 4410), span(0, 13230));
        sv::EventVector tooEarly { word(kMin - 1, S) };
        QCOMPARE(newSpan(tooEarly, 100), QString("none"));

        QCOMPARE(newSpan({}, 1000), span(1000, 1000 + kNew));
        QCOMPARE(newSpan({}, 0), span(0, kNew));
    }

    // Overlapping words from a file: the gap is between the latest end
    // before the click and the earliest start after it
    void new_word_among_overlapping_words() {
        sv::EventVector crossing { word(0, 30000), word(10000, 50000),
                                   word(60000, 100000) };
        QCOMPARE(newSpan(crossing, 40000), QString("none"));
        QCOMPARE(newSpan(crossing, 55000), span(50000, 60000));

        sv::EventVector within { word(0, 50000), word(10000, 20000),
                                 word(60000, 100000) };
        QCOMPARE(newSpan(within, 30000), QString("none"));
        QCOMPARE(newSpan(within, 55000), span(50000, 60000));
    }

    // Decision 10: the line of the nearer neighbour by the gap, of the
    // word before on a tie, of the only one if one, 0 if none
    void new_word_line() {
        sv::EventVector words { word(0, 10000, 0.f), word(30000, 40000, 1.f) };
        auto line = [&](frame_t s, frame_t e) {
            Span added;
            added.start = s;
            added.end = e;
            return LyricsEdit::newWordLine(words, added);
        };
        QCOMPARE(line(12000, 14000), 0.f);
        QCOMPARE(line(25000, 28000), 1.f);
        QCOMPARE(line(15000, 25000), 0.f);
        QCOMPARE(line(15000, 24999), 0.f);
        QCOMPARE(line(15001, 25000), 1.f);
        QCOMPARE(line(10000, 30000), 0.f);
        QCOMPARE(line(50000, 60000), 1.f);

        words = { word(20000, 30000, 3.f) };
        QCOMPARE(line(0, 10000), 3.f);
        QCOMPARE(line(40000, 50000), 3.f);

        words = {};
        QCOMPARE(line(0, 10000), 0.f);

        // The word before is the one ending latest, not the last to
        // start
        words = { word(0, 50000, 4.f), word(10000, 20000, 5.f),
                  word(80000, 90000, 6.f) };
        QCOMPARE(line(52000, 60000), 4.f);
        QCOMPARE(line(70000, 78000), 6.f);

        // From newWordSpan(), as the editor uses them together: just
        // after a line's last word, just before the next line's first,
        // and filling a gap between the two wholly
        const frame_t S = kSecond;
        words = { word(0, S, 0.f), word(2 * S, 3 * S, 1.f),
                  word(6 * S, 7 * S, 2.f), word(7 * S + 10000, 8 * S, 3.f) };
        Span added;
        QVERIFY(LyricsEdit::newWordSpan(words, S + 1000, kRate, added));
        QCOMPARE(span(added.start, added.end), span(S + 1000, S + 1000 + kNew));
        QCOMPARE(LyricsEdit::newWordLine(words, added), 0.f);
        QVERIFY(LyricsEdit::newWordSpan(words, 2 * S - 1000, kRate, added));
        QCOMPARE(span(added.start, added.end), span(2 * S - kNew, 2 * S));
        QCOMPARE(LyricsEdit::newWordLine(words, added), 1.f);
        QVERIFY(LyricsEdit::newWordSpan(words, 4 * S, kRate, added));
        QCOMPARE(LyricsEdit::newWordLine(words, added), 1.f);
        QVERIFY(LyricsEdit::newWordSpan(words, 6 * S - 1000, kRate, added));
        QCOMPARE(LyricsEdit::newWordLine(words, added), 2.f);
        QVERIFY(LyricsEdit::newWordSpan(words, 7 * S + 5000, kRate, added));
        QCOMPARE(span(added.start, added.end), span(7 * S, 7 * S + 10000));
        QCOMPARE(LyricsEdit::newWordLine(words, added), 2.f);
    }

    // Decision 11: typed text is cleaned as the parsers clean labels
    void typed_text_cleaned() {
        QCOMPARE(LyricsEdit::cleanText("hello"), QString("hello"));
        QCOMPARE(LyricsEdit::cleanText("  hello \t"), QString("hello"));
        QCOMPARE(LyricsEdit::cleanText("a\tb"), QString("a b"));
        QCOMPARE(LyricsEdit::cleanText(QString("a\x01" "b\x7f" "c")),
                 QString("abc"));
        QCOMPARE(LyricsEdit::cleanText(QString("a") + QChar(0xFFFE) + "b"),
                 QString("ab"));
        QCOMPARE(LyricsEdit::cleanText("a\nb"), QString("ab"));
        QCOMPARE(LyricsEdit::cleanText("<b>&amp;</b>"), QString("<b>&amp;</b>"));
        QCOMPARE(LyricsEdit::cleanText(QString::fromUtf8("syd\xc3\xa4n")),
                 QString::fromUtf8("syd\xc3\xa4n"));

        QString longText(Lyrics::maxLabelLength + 50, QChar('x'));
        QCOMPARE(LyricsEdit::cleanText(longText).size(),
                 qsizetype(Lyrics::maxLabelLength));

        // Nothing left: empty, which the edit refuses
        QCOMPARE(LyricsEdit::cleanText(""), QString());
        QVERIFY(LyricsEdit::cleanText("   ").isEmpty());
        QVERIFY(LyricsEdit::cleanText("\t\n").isEmpty());
        QVERIFY(LyricsEdit::cleanText(QString("\x01\x02")).isEmpty());
    }
};

#endif
