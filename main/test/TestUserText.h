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

#ifndef TEST_USER_TEXT_H
#define TEST_USER_TEXT_H

// Tier 1: figures as the user reads them

#include "../UserText.h"

#include <QObject>
#include <QtTest>

class TestUserText : public QObject
{
    Q_OBJECT

    static QString minus(QString s) { return QString(QChar(0x2212)) + s; }

private slots:
    // Tenths of a second, the minutes taken off after rounding: 59.97 s
    // is a minute, where it was "0:60.0"
    void minutes_and_seconds() {
        QCOMPARE(UserText::minutesAndSeconds(0.0), QString("0:00.0"));
        QCOMPARE(UserText::minutesAndSeconds(2.54), QString("0:02.5"));
        QCOMPARE(UserText::minutesAndSeconds(62.5), QString("1:02.5"));
        QCOMPARE(UserText::minutesAndSeconds(59.94), QString("0:59.9"));
        QCOMPARE(UserText::minutesAndSeconds(59.97), QString("1:00.0"));
        QCOMPARE(UserText::minutesAndSeconds(119.96), QString("2:00.0"));
        QCOMPARE(UserText::minutesAndSeconds(600.0), QString("10:00.0"));
        QCOMPARE(UserText::minutesAndSeconds(-1.25), minus("0:01.3"));
        QCOMPARE(UserText::minutesAndSeconds(-0.01), QString("0:00.0"));
    }

    void dbfs() {
        QCOMPARE(UserText::dbfs(-3.24), minus("3.2 dBFS"));
        QCOMPARE(UserText::dbfs(-0.04), QString("0.0 dBFS"));
        QCOMPARE(UserText::dbfs(0.0), QString("0.0 dBFS"));
        QCOMPARE(UserText::dbfs(-60.0), minus("60.0 dBFS"));
    }
};

#endif
