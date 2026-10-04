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

#ifndef TEST_INPUT_DEVICE_H
#define TEST_INPUT_DEVICE_H

// Tier 2: the input device chosen on a driver that lists its inputs, as
// a phone's does: which are offered, how a choice is kept, and the id
// it is opened by. No window and no device. The settings are an INI
// file of each test's own, as TestInputChannel's are.

#include "../InputDevice.h"

#include <QFile>
#include <QObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class TestInputDevice : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_counter = 0;
    QString m_path;

    static AudioRoute::Device device(int id, int type, QString name) {
        AudioRoute::Device d;
        d.id = id;
        d.type = type;
        d.productName = name;
        return d;
    }

    // As a Pixel lists them with a wireless receiver plugged in: two
    // microphones of its own, the receiver, the telephony line and a
    // Bluetooth headset's call microphone
    static std::vector<AudioRoute::Device> phoneList() {
        return { device(12, 15, "Pixel 9a"),
                 device(13, 15, "Pixel 9a"),
                 device(40, 11, "Wireless PRO RX"),
                 device(14, 18, "Pixel 9a"),
                 device(51, 7, "WF-1000XM6") };
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void init() {
        m_path = m_dir.filePath(QString("settings-%1.ini").arg(++m_counter));
    }

    void cleanup() {
        QFile::remove(m_path);
    }

    // The phone's microphone, a wired headset, USB; never a Bluetooth
    // call microphone, nor what is no microphone
    void the_types_offered() {
        for (int type : { 3, 11, 15, 22 }) {
            QVERIFY2(InputDevice::offered(type), qPrintable
                     (QString("type %1 is not offered").arg(type)));
        }
        for (int type : { 0, 7, 8, 18, 25, 26, 28 }) {
            QVERIFY2(!InputDevice::offered(type), qPrintable
                     (QString("type %1 is offered").arg(type)));
        }
    }

    // In the order listed, each type and name once: the first of the
    // phone's two microphones, and the receiver
    void the_choices_of_a_list() {
        const auto choices = InputDevice::choices(phoneList());
        QCOMPARE(int(choices.size()), 2);
        QCOMPARE(choices[0].id, 12);
        QCOMPARE(choices[1].id, 40);
        QCOMPARE(AudioRoute::deviceName(choices[1]),
                 QString("USB device (Wireless PRO RX)"));
    }

    // Kept by type and product name, not by id, for each driver; none
    // chosen is no key
    void a_choice_is_kept_by_type_and_name() {
        QSettings settings(m_path, QSettings::IniFormat);
        AudioRoute::Device kept;
        QVERIFY(!InputDevice::chosen(settings, "oboe", kept));

        InputDevice::choose(settings, "oboe", device(40, 11, "Wireless PRO RX"));
        QVERIFY(InputDevice::chosen(settings, "oboe", kept));
        QCOMPARE(kept.type, 11);
        QCOMPARE(kept.productName, QString("Wireless PRO RX"));
        QCOMPARE(kept.id, 0);
        QVERIFY(!InputDevice::chosen(settings, "other", kept));

        // A name with the separator in it comes back whole
        InputDevice::choose(settings, "oboe", device(7, 22, "A|B / C"));
        QVERIFY(InputDevice::chosen(settings, "oboe", kept));
        QCOMPARE(kept.type, 22);
        QCOMPARE(kept.productName, QString("A|B / C"));

        InputDevice::choose(settings, "oboe", AudioRoute::Device());
        QVERIFY(!InputDevice::chosen(settings, "oboe", kept));
        settings.beginGroup("InputDevice");
        QVERIFY(settings.childKeys().isEmpty());
        settings.endGroup();
    }

    // The receiver plugged in again has another id: it is opened by
    // that. Not plugged in, or nothing chosen, it is Android's choice
    void the_id_to_open() {
        auto listed = phoneList();
        QCOMPARE(InputDevice::idToOpen(listed, device(0, 11, "Wireless PRO RX")),
                 40);
        listed[2].id = 77;
        QCOMPARE(InputDevice::idToOpen(listed, device(0, 11, "Wireless PRO RX")),
                 77);
        QCOMPARE(InputDevice::idToOpen(listed, device(0, 15, "Pixel 9a")), 12);
        QCOMPARE(InputDevice::idToOpen(listed, device(0, 11, "Wireless GO")), 0);
        QCOMPARE(InputDevice::idToOpen(listed, device(0, 22, "Wireless PRO RX")),
                 0);
        QCOMPARE(InputDevice::idToOpen(listed, AudioRoute::Device()), 0);
    }
};

#endif
