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

#ifndef TEST_SONG_SCROLL_H
#define TEST_SONG_SCROLL_H

// Tier 2: the arithmetic of the song scroll bar. Frames, pixels and
// pitches in, frames, pixels and columns out; no widget.

#include "../SongScroll.h"

#include <QObject>
#include <QtTest>

#include <cmath>
#include <utility>
#include <vector>

class TestSongScroll : public QObject
{
    Q_OBJECT

    typedef sv::sv_frame_t frame_t;
    typedef std::vector<std::pair<frame_t, double>> Events;

    // Ten seconds on a strip 882 pixels wide: 500 frames to a pixel
    static constexpr frame_t song = 441000;
    static constexpr double width = 882.0;
    static constexpr double minWidth = 16.0;

    static QString text(const SongScroll::Thumb &t) {
        return QString("[%1, %2]").arg(t.x0).arg(t.x1);
    }

    static void verifyThumb(const SongScroll::Thumb &t, double x0, double x1) {
        QVERIFY2(std::fabs(t.x0 - x0) < 1e-9 && std::fabs(t.x1 - x1) < 1e-9,
                 qPrintable(QString("the thumb is %1, not [%2, %3]")
                            .arg(text(t)).arg(x0).arg(x1)));
    }

    // Voiced frames every step from first up to (not including) end
    static void addEvents(Events &events, frame_t first, frame_t end,
                          frame_t step, double value) {
        for (frame_t f = first; f < end; f += step) {
            events.push_back({ f, value });
        }
    }

    static QString voiced(const std::vector<SongScroll::Column> &columns) {
        QString s;
        for (const auto &c : columns) s += (c.isEmpty() ? "." : "#");
        return s;
    }

private slots:
    void frame_to_x_and_back() {
        QCOMPARE(SongScroll::xForFrame(0, song, width), 0.0);
        QCOMPARE(SongScroll::xForFrame(song, song, width), width);
        QCOMPARE(SongScroll::xForFrame(song / 2, song, width), width / 2);
        QCOMPARE(SongScroll::frameForX(width / 2, song, width), song / 2);

        for (frame_t f : { frame_t(0), frame_t(1), frame_t(499),
                           frame_t(12345), frame_t(220500), song - 1 }) {
            QCOMPARE(SongScroll::frameForX
                     (SongScroll::xForFrame(f, song, width), song, width), f);
        }

        // Back from a frame to within the half a frame it was rounded by
        double halfFrame = 0.5 * width / double(song);
        for (double x = 0.0; x <= width; x += 37.3) {
            double back = SongScroll::xForFrame
                (SongScroll::frameForX(x, song, width), song, width);
            QVERIFY2(std::fabs(back - x) <= halfFrame,
                     qPrintable(QString("%1 came back as %2").arg(x).arg(back)));
        }

        // No song, or no strip: nothing to map
        QCOMPARE(SongScroll::xForFrame(1000, 0, width), 0.0);
        QCOMPARE(SongScroll::frameForX(100.0, 0, width), frame_t(0));
        QCOMPARE(SongScroll::frameForX(100.0, song, 0.0), frame_t(0));
    }

    void frames_are_kept_within_the_song() {
        QCOMPARE(SongScroll::clampToSong(-5, song), frame_t(0));
        QCOMPARE(SongScroll::clampToSong(0, song), frame_t(0));
        QCOMPARE(SongScroll::clampToSong(1234, song), frame_t(1234));
        QCOMPARE(SongScroll::clampToSong(song - 1, song), song - 1);
        QCOMPARE(SongScroll::clampToSong(song, song), song - 1);
        QCOMPARE(SongScroll::clampToSong(song + 100, song), song - 1);
        QCOMPARE(SongScroll::clampToSong(100, 0), frame_t(0));
    }

    void thumb_covers_what_the_panes_show() {
        verifyThumb(SongScroll::thumb(100000, 150000, song, width, minWidth),
                    200.0, 300.0);
    }

    // A pane scrolled to the song's start shows some of what is before
    // it, which the strip does not have: the thumb stops at the edge
    void thumb_at_the_start() {
        verifyThumb(SongScroll::thumb(-20000, 30000, song, width, minWidth),
                    0.0, 60.0);

        // and one narrower than the least is widened into the strip
        verifyThumb(SongScroll::thumb(-1000, 2000, song, width, minWidth),
                    0.0, minWidth);
        verifyThumb(SongScroll::thumb(0, 500, song, width, minWidth),
                    0.0, minWidth);
    }

    void thumb_at_the_end() {
        verifyThumb(SongScroll::thumb(song - 30000, song + 20000,
                                      song, width, minWidth),
                    width - 60.0, width);

        // 10 pixels of song, widened to 16 about their middle and moved
        // back from the edge
        verifyThumb(SongScroll::thumb(song - 5000, song + 5000,
                                      song, width, minWidth),
                    width - minWidth, width);

        // Nothing of the song on show at all: at the end still
        verifyThumb(SongScroll::thumb(song + 1000, song + 50000,
                                      song, width, minWidth),
                    width - minWidth, width);
    }

    // Narrow in the middle: the least width, about the same middle
    void thumb_has_a_least_width() {
        verifyThumb(SongScroll::thumb(220000, 221000, song, width, minWidth),
                    441.0 - minWidth / 2, 441.0 + minWidth / 2);
    }

    // Zoomed out beyond the song: the whole strip
    void thumb_wider_than_the_song() {
        verifyThumb(SongScroll::thumb(-100000, song + 100000,
                                      song, width, minWidth),
                    0.0, width);
        verifyThumb(SongScroll::thumb(0, song, song, width, minWidth),
                    0.0, width);

        // and a strip narrower than the least thumb is all thumb
        verifyThumb(SongScroll::thumb(1000, 2000, song, 10.0, minWidth),
                    0.0, 10.0);

        // No song: no thumb
        QCOMPARE(SongScroll::thumb(0, 1000, 0, width, minWidth).width(), 0.0);
    }

    void press_on_the_thumb_or_beside_it() {
        SongScroll::Thumb t;
        t.x0 = 200.0;
        t.x1 = 300.0;
        QVERIFY(!SongScroll::hitsThumb(t, 199.9));
        QVERIFY(SongScroll::hitsThumb(t, 200.0));
        QVERIFY(SongScroll::hitsThumb(t, 250.0));
        QVERIFY(SongScroll::hitsThumb(t, 300.0));
        QVERIFY(!SongScroll::hitsThumb(t, 300.1));
    }

    // A drag moves the centre by what the strip shows in the distance,
    // from wherever it was grabbed, and not out of the song
    void drag_moves_the_centre_by_the_distance() {
        QCOMPARE(SongScroll::dragCentre(125000, 250.0, 250.0, song, width),
                 frame_t(125000));
        QCOMPARE(SongScroll::dragCentre(125000, 250.0, 300.0, song, width),
                 frame_t(150000));
        QCOMPARE(SongScroll::dragCentre(125000, 250.0, 200.0, song, width),
                 frame_t(100000));
        QCOMPARE(SongScroll::dragCentre(125000, 250.0, 250.5, song, width),
                 frame_t(125250));

        // The same distance from another grab is the same movement
        QCOMPARE(SongScroll::dragCentre(125000, 20.0, 70.0, song, width),
                 frame_t(150000));

        QCOMPARE(SongScroll::dragCentre(125000, 250.0, -500.0, song, width),
                 frame_t(0));
        QCOMPARE(SongScroll::dragCentre(125000, 250.0, 5000.0, song, width),
                 song - 1);
    }

    void jump_centres_on_the_frame_there() {
        QCOMPARE(SongScroll::jumpCentre(441.0, song, width), frame_t(220500));
        QCOMPARE(SongScroll::jumpCentre(1.0, song, width), frame_t(500));
        QCOMPARE(SongScroll::jumpCentre(-10.0, song, width), frame_t(0));
        QCOMPARE(SongScroll::jumpCentre(width + 10.0, song, width), song - 1);
    }

    // Each column has the lowest and highest pitch of its frames
    void pitch_columns_have_their_range() {
        Events events;
        for (int i = 0; i < 10; ++i) events.push_back({ i * 10, 100.0 + i });
        events.push_back({ 150, 220.0 });
        events.push_back({ 160, 210.0 });

        std::vector<SongScroll::Column> columns =
            SongScroll::pitchColumns(events, 10, 1000, 10);
        QCOMPARE(int(columns.size()), 10);
        QCOMPARE(columns[0].low, 100.0);
        QCOMPARE(columns[0].high, 109.0);
        QCOMPARE(columns[1].low, 210.0);
        QCOMPARE(columns[1].high, 220.0);
        QCOMPARE(voiced(columns), QString("##........"));

        double low = 0, high = 0;
        QVERIFY(SongScroll::pitchRange(columns, low, high));
        QCOMPARE(low, 100.0);
        QCOMPARE(high, 220.0);
    }

    // Where the pitch track has no events, as in a rest, and where it
    // has events of no pitch, the columns are empty
    void pitch_columns_empty_for_a_gap() {
        Events events;
        addEvents(events, 0, 300, 10, 200.0);
        addEvents(events, 400, 500, 10, 0.0);
        addEvents(events, 500, 550, 10, -1.0);
        addEvents(events, 600, 1000, 10, 300.0);

        std::vector<SongScroll::Column> columns =
            SongScroll::pitchColumns(events, 10, 1000, 10);
        QCOMPARE(voiced(columns), QString("###...####"));

        // and nothing at all is all empty
        columns = SongScroll::pitchColumns(Events(), 10, 1000, 10);
        QCOMPARE(voiced(columns), QString(".........."));
        double low = 0, high = 0;
        QVERIFY(!SongScroll::pitchRange(columns, low, high));
    }

    // A pitch track has an event for each hop: in columns narrower than
    // that the voiced part has no holes
    void pitch_columns_narrower_than_an_event() {
        Events events;
        addEvents(events, 0, 500, 50, 200.0);
        std::vector<SongScroll::Column> columns =
            SongScroll::pitchColumns(events, 50, 1000, 100);
        QCOMPARE(int(columns.size()), 100);
        QCOMPARE(voiced(columns),
                 QString(50, '#') + QString(50, '.'));
    }

    // What is outside the song is left out, and what overlaps its ends
    // is cut there
    void pitch_columns_within_the_song() {
        Events events;
        events.push_back({ -100, 150.0 }); // ends before the song
        events.push_back({ -20, 160.0 });  // frames -20 to 29
        events.push_back({ 990, 170.0 });  // to 1039, the song ends at 999
        events.push_back({ 1000, 180.0 }); // after the song
        std::vector<SongScroll::Column> columns =
            SongScroll::pitchColumns(events, 50, 1000, 100);
        QCOMPARE(voiced(columns),
                 QString("###") + QString(96, '.') + QString("#"));
        QCOMPARE(columns[0].low, 160.0);
        QCOMPARE(columns[99].high, 170.0);

        // No columns, no song
        QVERIFY(SongScroll::pitchColumns(events, 50, 1000, 0).empty());
        QCOMPARE(voiced(SongScroll::pitchColumns(events, 50, 0, 4)),
                 QString("...."));
    }

    // Octaves are equal steps; low at the bottom, high at the top
    void pitch_on_a_log_scale() {
        QCOMPARE(SongScroll::yForPitch(100.0, 100.0, 400.0, 16.0), 16.0);
        QCOMPARE(SongScroll::yForPitch(400.0, 100.0, 400.0, 16.0), 0.0);
        QVERIFY(std::fabs(SongScroll::yForPitch(200.0, 100.0, 400.0, 16.0)
                          - 8.0) < 1e-9);

        // Out of the range: at its edge
        QCOMPARE(SongScroll::yForPitch(50.0, 100.0, 400.0, 16.0), 16.0);
        QCOMPARE(SongScroll::yForPitch(800.0, 100.0, 400.0, 16.0), 0.0);

        // A song of one pitch: in the middle
        QCOMPARE(SongScroll::yForPitch(220.0, 220.0, 220.0, 16.0), 8.0);
    }
};

#endif
