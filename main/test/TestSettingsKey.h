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

#ifndef TEST_SETTINGS_KEY_H
#define TEST_SETTINGS_KEY_H

// Tier 1: a device's name as part of a settings key

#include "../SettingsKey.h"

#include <QObject>
#include <QtTest>

class TestSettingsKey : public QObject
{
    Q_OBJECT

private slots:
    // What QSettings would read as a subgroup, and the separator, are
    // encoded, "%" first so that the encoding reads back; nothing else
    void names_are_encoded_and_read_back() {
        const QString name = QString::fromUtf8
            ("Line In 1/2 \\ Mic | 100% \xc3\x96\xc3\xb6");
        const QString key = SettingsKey::encoded(name);
        QVERIFY(!key.contains("/"));
        QVERIFY(!key.contains("\\"));
        QVERIFY(!key.contains("|"));
        QCOMPARE(key, QString::fromUtf8
                 ("Line In 1%2F2 %5C Mic %7C 100%25 \xc3\x96\xc3\xb6"));
        QCOMPARE(SettingsKey::decoded(key), name);
        QCOMPARE(SettingsKey::encoded("%2F"), QString("%252F"));
        QCOMPARE(SettingsKey::decoded("%252F"), QString("%2F"));
        QCOMPARE(SettingsKey::encoded("USB device (Wireless PRO RX)"),
                 QString("USB device (Wireless PRO RX)"));
    }
};

#endif
