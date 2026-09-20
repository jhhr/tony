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

#ifndef TEST_COVERAGE_H
#define TEST_COVERAGE_H

// Tier 2: the list of recorded ranges of a singing take

#include "../Coverage.h"

#include <QObject>
#include <QtTest>

class TestCoverage : public QObject
{
    Q_OBJECT

    typedef Coverage::Range Range;
    typedef Coverage::Ranges Ranges;

private slots:
    void empty() {
        Coverage c;
        QVERIFY(c.isEmpty());
        QVERIFY(!c.contains(0));
        QVERIFY(!c.overlaps(0, 100));
        QCOMPARE(c.getEndFrame(), sv::sv_frame_t(0));
        c.remove(0, 100);
        QVERIFY(c.isEmpty());
    }

    void add_keeps_order() {
        Coverage c;
        c.add(500, 600);
        c.add(100, 200);
        c.add(300, 400);
        QCOMPARE(c.getRanges(),
                 (Ranges { Range(100, 200), Range(300, 400), Range(500, 600) }));
        QCOMPARE(c.getEndFrame(), sv::sv_frame_t(600));
    }

    void add_ignores_empty_ranges() {
        Coverage c;
        c.add(100, 100);
        c.add(200, 100);
        QVERIFY(c.isEmpty());
    }

    void add_joins_overlapping() {
        Coverage c;
        c.add(100, 200);
        c.add(300, 400);
        c.add(500, 600);
        c.add(150, 350);
        QCOMPARE(c.getRanges(), (Ranges { Range(100, 400), Range(500, 600) }));
        c.add(0, 1000);
        QCOMPARE(c.getRanges(), (Ranges { Range(0, 1000) }));
    }

    // A recording that starts where the last one stopped is one
    // stretch of singing, not two
    void add_joins_touching() {
        Coverage c;
        c.add(100, 200);
        c.add(200, 300);
        QCOMPARE(c.getRanges(), (Ranges { Range(100, 300) }));
        c.add(50, 100);
        QCOMPARE(c.getRanges(), (Ranges { Range(50, 300) }));
        c.add(301, 400);
        QCOMPARE(c.getRanges(), (Ranges { Range(50, 300), Range(301, 400) }));
    }

    void add_inside_changes_nothing() {
        Coverage c;
        c.add(100, 400);
        c.add(200, 300);
        QCOMPARE(c.getRanges(), (Ranges { Range(100, 400) }));
    }

    void remove_whole() {
        Coverage c;
        c.add(100, 200);
        c.add(300, 400);
        c.remove(300, 400);
        QCOMPARE(c.getRanges(), (Ranges { Range(100, 200) }));
        c.remove(0, 1000);
        QVERIFY(c.isEmpty());
    }

    void remove_trims() {
        Coverage c;
        c.add(100, 400);
        c.remove(50, 150);
        QCOMPARE(c.getRanges(), (Ranges { Range(150, 400) }));
        c.remove(350, 500);
        QCOMPARE(c.getRanges(), (Ranges { Range(150, 350) }));
    }

    void remove_splits() {
        Coverage c;
        c.add(100, 400);
        c.remove(200, 300);
        QCOMPARE(c.getRanges(), (Ranges { Range(100, 200), Range(300, 400) }));
    }

    void remove_across_several() {
        Coverage c;
        c.add(100, 200);
        c.add(300, 400);
        c.add(500, 600);
        c.remove(150, 550);
        QCOMPARE(c.getRanges(), (Ranges { Range(100, 150), Range(550, 600) }));
    }

    void remove_outside_changes_nothing() {
        Coverage c;
        c.add(100, 200);
        c.remove(200, 300);
        c.remove(0, 100);
        c.remove(150, 150);
        QCOMPARE(c.getRanges(), (Ranges { Range(100, 200) }));
    }

    // The end is not part of the range
    void contains_and_range_at() {
        Coverage c;
        c.add(100, 200);
        c.add(300, 400);
        QVERIFY(!c.contains(99));
        QVERIFY(c.contains(100));
        QVERIFY(c.contains(199));
        QVERIFY(!c.contains(200));
        QVERIFY(!c.contains(250));

        Range r(7, 8);
        QVERIFY(c.getRangeAt(350, r));
        QCOMPARE(r, Range(300, 400));
        QVERIFY(!c.getRangeAt(250, r));
        QCOMPARE(r, Range(300, 400));
    }

    void overlaps() {
        Coverage c;
        c.add(100, 200);
        QVERIFY(!c.overlaps(0, 100));
        QVERIFY(c.overlaps(0, 101));
        QVERIFY(c.overlaps(150, 160));
        QVERIFY(c.overlaps(199, 300));
        QVERIFY(!c.overlaps(200, 300));
        QVERIFY(!c.overlaps(150, 150));
    }

    void equality() {
        Coverage a, b;
        a.add(100, 200);
        a.add(200, 300);
        b.add(100, 300);
        QVERIFY(a == b);
        b.remove(150, 160);
        QVERIFY(a != b);
    }

    // Coverage is stored as the regions of the coverage strip's model,
    // so it has to go there and come back unchanged

    void events_round_trip() {
        Coverage c;
        c.add(100, 200);
        c.add(4000, 9000);

        sv::EventVector events = c.toEvents();
        QCOMPARE(int(events.size()), 2);
        QCOMPARE(events[0].getFrame(), sv::sv_frame_t(100));
        QCOMPARE(events[0].getDuration(), sv::sv_frame_t(100));
        QCOMPARE(events[1].getFrame(), sv::sv_frame_t(4000));
        QCOMPARE(events[1].getDuration(), sv::sv_frame_t(5000));

        // A stock RegionLayer prints the value of a region that has no
        // label of its own, so every region has one
        for (const sv::Event &e : events) {
            QCOMPARE(e.getValue(), 0.f);
            QCOMPARE(e.getLabel(), Coverage::regionLabel());
        }

        QCOMPARE(Coverage::fromEvents(events).getRanges(), c.getRanges());
    }

    void events_of_nothing() {
        QVERIFY(Coverage().toEvents().empty());
        QVERIFY(Coverage::fromEvents(sv::EventVector()).isEmpty());
    }

    void events_sorted_and_joined() {
        // Whatever order a model hands its events back in, and whether
        // or not two of them meet, what comes out is coverage
        sv::EventVector events;
        events.push_back(sv::Event(500, 0.f, 100, Coverage::regionLabel()));
        events.push_back(sv::Event(100, 0.f, 100, Coverage::regionLabel()));
        events.push_back(sv::Event(200, 0.f, 100, Coverage::regionLabel()));

        QCOMPARE(Coverage::fromEvents(events).getRanges(),
                 (Ranges { Range(100, 300), Range(500, 600) }));
    }
};

#endif
