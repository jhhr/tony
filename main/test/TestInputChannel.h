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

#ifndef TEST_INPUT_CHANNEL_H
#define TEST_INPUT_CHANNEL_H

// Tier 2: the input channel a take is made from, as the settings keep it
// for each input device, and the channel of a recording that it comes
// to. No window and no device. The settings are an INI file of each
// test's own, as TestVoiceThreshold's are.

#include "../InputChannel.h"

#include <QFile>
#include <QObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class TestInputChannel : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_counter = 0;
    QString m_path;

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

    // Both, then each input, counted from 1 for the user and from 0 as a
    // file's channels are
    void the_choices() {
        QCOMPARE(InputChannel::choices(),
                 (std::vector<int>{ InputChannel::kBoth, 0, 1 }));
        QCOMPARE(InputChannel::label(InputChannel::kBoth),
                 QString("Both Inputs"));
        QCOMPARE(InputChannel::label(0), QString("Input 1"));
        QCOMPARE(InputChannel::label(1), QString("Input 2"));
        QVERIFY(!InputChannel::isSingle(InputChannel::kBoth));
        QVERIFY(InputChannel::isSingle(0));
        QVERIFY(InputChannel::isSingle(1));
        QVERIFY(InputChannel::offered(InputChannel::kBoth));
        QVERIFY(InputChannel::offered(1));
        QVERIFY(!InputChannel::offered(2));
        QVERIFY(!InputChannel::offered(-2));
    }

    // Nothing kept is Both, what a take was made of before there was a
    // choice
    void absent_is_both() {
        QSettings settings(m_path, QSettings::IniFormat);
        QCOMPARE(InputChannel::channel(settings, { "wasapi", "Mic" }),
                 InputChannel::kBoth);
        QCOMPARE(InputChannel::channel(settings, { "", "" }),
                 InputChannel::kBoth);
    }

    // Kept for each input device: another device, or the same name under
    // another driver, has its own; the playback device has nothing to do
    // with it
    void kept_per_input_device() {
        QSettings settings(m_path, QSettings::IniFormat);
        InputChannel::setChannel(settings, { "wasapi", "AI-Micro" }, 0);
        InputChannel::setChannel(settings, { "wasapi", "" }, 1);
        InputChannel::setChannel(settings, { "oboe", "USB device AI-Micro" },
                                 1);
        QCOMPARE(InputChannel::channel(settings, { "wasapi", "AI-Micro" }), 0);
        QCOMPARE(InputChannel::channel(settings, { "wasapi", "" }), 1);
        QCOMPARE(InputChannel::channel(settings, { "mme", "AI-Micro" }),
                 InputChannel::kBoth);
        QCOMPARE(InputChannel::channel(settings, { "wasapi", "Webcam" }),
                 InputChannel::kBoth);
        QCOMPARE(InputChannel::channel
                 (settings, { "oboe", "USB device AI-Micro" }), 1);

        // Read back from the file, as the next launch reads it
        settings.sync();
        QSettings again(m_path, QSettings::IniFormat);
        QCOMPARE(InputChannel::channel(again, { "wasapi", "AI-Micro" }), 0);
        QCOMPARE(InputChannel::channel(again, { "wasapi", "" }), 1);
    }

    // Both is kept as no key at all, and a choice can be changed back
    void both_is_no_key() {
        QSettings settings(m_path, QSettings::IniFormat);
        const InputChannel::Key key { "wasapi", "AI-Micro" };
        InputChannel::setChannel(settings, key, 1);
        QVERIFY(!settings.allKeys().isEmpty());
        InputChannel::setChannel(settings, key, InputChannel::kBoth);
        QCOMPARE(InputChannel::channel(settings, key), InputChannel::kBoth);
        QVERIFY(settings.allKeys().isEmpty());
    }

    // Device names hold what QSettings would take for subgroups, and the
    // separator between the names: neither runs into another device's
    void names_that_look_like_groups() {
        QSettings settings(m_path, QSettings::IniFormat);
        InputChannel::setChannel(settings, { "wasapi", "Mic/Line (ä)" }, 1);
        InputChannel::setChannel(settings, { "wasapi|Mic", "Line" }, 0);
        settings.sync();
        QSettings again(m_path, QSettings::IniFormat);
        QCOMPARE(InputChannel::channel(again, { "wasapi", "Mic/Line (ä)" }), 1);
        QCOMPARE(InputChannel::channel(again, { "wasapi", "Mic" }),
                 InputChannel::kBoth);
        QCOMPARE(InputChannel::channel(again, { "wasapi|Mic", "Line" }), 0);
        QCOMPARE(InputChannel::channel(again, { "wasapi", "Mic|Line" }),
                 InputChannel::kBoth);
    }

    // What is kept that is none of the choices reads as Both
    void a_kept_value_not_offered_is_both() {
        QSettings settings(m_path, QSettings::IniFormat);
        const InputChannel::Key key { "wasapi", "AI-Micro" };
        InputChannel::setChannel(settings, key, 1);
        const QString stored = settings.allKeys().value(0);
        QVERIFY(stored != "");
        for (QString bad : { "3", "0", "-1", "two", "" }) {
            settings.setValue(stored, bad);
            QVERIFY2(InputChannel::channel(settings, key) ==
                     InputChannel::kBoth, qPrintable(bad));
        }
        // and none is written for a channel not offered
        InputChannel::setChannel(settings, key, 5);
        QCOMPARE(InputChannel::channel(settings, key), InputChannel::kBoth);
        QVERIFY(settings.allKeys().isEmpty());
    }

    // The input a phone's driver last recorded from, kept apart for each
    // driver and from the choices
    void the_last_input() {
        QSettings settings(m_path, QSettings::IniFormat);
        QCOMPARE(InputChannel::lastInput(settings, "oboe"), QString());
        InputChannel::setLastInput(settings, "oboe", "USB device AI-Micro");
        QCOMPARE(InputChannel::lastInput(settings, "oboe"),
                 QString("USB device AI-Micro"));
        QCOMPARE(InputChannel::lastInput(settings, "wasapi"), QString());
        QCOMPARE(InputChannel::channel(settings, { "oboe", "" }),
                 InputChannel::kBoth);
        InputChannel::setChannel(settings, { "oboe", "USB device AI-Micro" },
                                 0);
        QCOMPARE(InputChannel::lastInput(settings, "oboe"),
                 QString("USB device AI-Micro"));
    }

    // The inputs that carry the microphone: within 20 dB of the loudest
    void the_inputs_that_carry_the_microphone() {
        QCOMPARE(InputChannel::carrying({}), std::vector<int>());
        QCOMPARE(InputChannel::carrying({ 0.f, 0.f }), std::vector<int>());
        QCOMPARE(InputChannel::carrying({ 0.5f }), std::vector<int>{ 0 });
        QCOMPARE(InputChannel::carrying({ 0.5f, 0.001f }),
                 std::vector<int>{ 0 });
        QCOMPARE(InputChannel::carrying({ 0.0005f, 0.25f }),
                 std::vector<int>{ 1 });
        QCOMPARE(InputChannel::carrying({ 0.5f, 0.06f }),
                 (std::vector<int>{ 0, 1 }));
        QCOMPARE(InputChannel::carrying({ 0.5f, 0.04f }),
                 std::vector<int>{ 0 });
    }

    // The channel of a recording a take is made from: the one chosen if
    // it has it, else all of them
    void the_channel_of_a_recording() {
        QCOMPARE(InputChannel::effectiveChannel(InputChannel::kBoth, 2),
                 InputChannel::kBoth);
        QCOMPARE(InputChannel::effectiveChannel(0, 2), 0);
        QCOMPARE(InputChannel::effectiveChannel(1, 2), 1);
        QCOMPARE(InputChannel::effectiveChannel(0, 1), 0);
        QCOMPARE(InputChannel::effectiveChannel(1, 1), InputChannel::kBoth);
        QCOMPARE(InputChannel::effectiveChannel(1, 0), InputChannel::kBoth);
    }
};

#endif
