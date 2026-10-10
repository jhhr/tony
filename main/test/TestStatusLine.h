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

#ifndef TEST_STATUS_LINE_H
#define TEST_STATUS_LINE_H

// Tier 1: what has the status bar over what the views write

#include "../StatusLine.h"

#include <QObject>
#include <QSignalSpy>
#include <QtTest>

class TestStatusLine : public QObject
{
    Q_OBJECT

private slots:
    // Nothing set, the views write it
    void empty_leaves_it_to_the_views() {
        StatusLine line;
        QCOMPARE(line.text(""), QString());
    }

    // The countdown over a notice, a notice over the held message
    void the_countdown_then_a_notice_then_the_held_message() {
        StatusLine line;
        line.setHeld("Take: peak -12 dBFS");
        QCOMPARE(line.text(""), QString("Take: peak -12 dBFS"));
        line.setNotice("Not plugged in");
        QCOMPARE(line.text(""), QString("Not plugged in"));
        QCOMPARE(line.text("Lead-in: 2"), QString("Lead-in: 2"));
        line.setNotice("");
        QCOMPARE(line.text(""), QString("Take: peak -12 dBFS"));
        line.clearHeld();
        QCOMPARE(line.text(""), QString());
    }

    // A notice runs out, says so, and what was under it is what belongs
    // there again: the held message, not the stale notice
    void a_notice_runs_out_to_the_held_message() {
        StatusLine line;
        line.setNoticeMs(100);
        line.setHeld("Take: peak -12 dBFS");
        QSignalSpy expired(&line, &StatusLine::expired);
        line.setNotice("Not plugged in");
        QCOMPARE(line.text(""), QString("Not plugged in"));
        QVERIFY(expired.wait(2000));
        QCOMPARE(expired.count(), 1);
        QCOMPARE(line.notice(), QString());
        QCOMPARE(line.text(""), QString("Take: peak -12 dBFS"));
    }

    // Another notice starts the time again; one cleared does not expire
    void a_new_notice_starts_its_time_again() {
        StatusLine line;
        line.setNoticeMs(300);
        QSignalSpy expired(&line, &StatusLine::expired);
        line.setNotice("First");
        QTest::qWait(200);
        line.setNotice("Second");
        QTest::qWait(200);
        QCOMPARE(expired.count(), 0);
        QCOMPARE(line.text(""), QString("Second"));
        QVERIFY(expired.wait(2000));
        QCOMPARE(line.text(""), QString());

        line.setNotice("Third");
        line.setNotice("");
        QTest::qWait(400);
        QCOMPARE(expired.count(), 1);
    }
};

#endif
