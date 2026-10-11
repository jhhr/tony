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

#ifndef TEST_DEVICE_SWITCH_H
#define TEST_DEVICE_SWITCH_H

// Tier 1: the output device to switch back to

#include "../DeviceSwitch.h"

#include <QObject>
#include <QtTest>

class TestDeviceSwitch : public QObject
{
    Q_OBJECT

    static QString previous(const DeviceSwitch &s, QString key) {
        QString device = "(untouched)";
        if (!s.previous(key, device)) return "(none)";
        return device;
    }

private slots:
    // Until a second device is chosen there is nothing to go back to
    void none_at_first() {
        DeviceSwitch s;
        QCOMPARE(previous(s, "audio-playback-device-wasapi"), QString("(none)"));
    }

    // The device left is the one to go back to; going back to it makes
    // the other the one, so that the hotkey goes to and fro
    void the_device_left_then_to_and_fro() {
        const QString key = "audio-playback-device-wasapi";
        DeviceSwitch s;
        s.chosen(key, "Headphones", "Speakers");
        QCOMPARE(previous(s, key), QString("Headphones"));
        s.chosen(key, "Speakers", "Headphones");
        QCOMPARE(previous(s, key), QString("Speakers"));
        s.chosen(key, "Headphones", "Speakers");
        QCOMPARE(previous(s, key), QString("Headphones"));
    }

    // The one in use chosen again changes nothing
    void the_same_device_again() {
        const QString key = "audio-playback-device";
        DeviceSwitch s;
        s.chosen(key, "Speakers", "Speakers");
        QCOMPARE(previous(s, key), QString("(none)"));
        s.chosen(key, "Headphones", "Speakers");
        s.chosen(key, "Speakers", "Speakers");
        QCOMPARE(previous(s, key), QString("Headphones"));
    }

    // (System Default) is a device like any other
    void the_system_default() {
        const QString key = "audio-playback-device";
        DeviceSwitch s;
        s.chosen(key, "", "Speakers");
        QCOMPARE(previous(s, key), QString(""));
        s.chosen(key, "Speakers", "");
        QCOMPARE(previous(s, key), QString("Speakers"));
    }

    // Each driver has its devices under a key of its own: a device kept
    // for one is none for another, and a device chosen under another
    // takes its place
    void another_driver() {
        DeviceSwitch s;
        s.chosen("audio-playback-device-mme", "Headphones", "Speakers");
        QCOMPARE(previous(s, "audio-playback-device-wasapi"),
                 QString("(none)"));
        QCOMPARE(previous(s, "audio-playback-device-mme"),
                 QString("Headphones"));
        s.chosen("audio-playback-device-wasapi", "Speakers (W)",
                 "Headphones (W)");
        QCOMPARE(previous(s, "audio-playback-device-mme"), QString("(none)"));
        QCOMPARE(previous(s, "audio-playback-device-wasapi"),
                 QString("Speakers (W)"));
    }
};

#endif
