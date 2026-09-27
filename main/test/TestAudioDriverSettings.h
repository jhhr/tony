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

#ifndef TEST_AUDIO_DRIVER_SETTINGS_H
#define TEST_AUDIO_DRIVER_SETTINGS_H

// Tier 2: the audio driver and the latency asked of it, as the
// Preferences keep them. No window and no device. The settings are an
// INI file of each test's own, as TestLatencyCalibration's are.

#include "../AudioDriverSettings.h"

#include <QFile>
#include <QObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class TestAudioDriverSettings : public QObject
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

    // The drivers among bqaudioio's implementations, in the order they
    // are offered in, whatever order the factory gives them in
    void drivers_in_their_order() {
        QCOMPARE(AudioDriverSettings::drivers
                 ({ "pulse", "wasapi", "port", "directsound", "jack", "mme" }),
                 QStringList({ "mme", "directsound", "wasapi" }));
        QCOMPARE(AudioDriverSettings::drivers({ "pulse", "port", "jack" }),
                 QStringList());
        QCOMPARE(AudioDriverSettings::drivers({ "port", "wasapi" }),
                 QStringList({ "wasapi" }));
    }

    // WASAPI is named where nothing is, or "auto", and the devices
    // chosen before there were drivers become WASAPI's, where it has
    // none of its own; MME where there is no WASAPI. Then, or where
    // anything else is named, nothing happens
    void default_driver_named_once() {
        QSettings settings(m_path, QSettings::IniFormat);
        const QStringList windows = { "port", "mme", "directsound", "wasapi" };

        QCOMPARE(AudioDriverSettings::defaultDriver(windows),
                 QString("wasapi"));
        QCOMPARE(AudioDriverSettings::defaultDriver
                 ({ "port", "mme", "directsound" }), QString("mme"));
        QCOMPARE(AudioDriverSettings::defaultDriver({ "pulse", "port" }),
                 QString());

        settings.setValue("Preferences/audio-playback-device", "Speakers");
        settings.setValue("Preferences/audio-record-device", "Mic");
        settings.setValue("Preferences/audio-record-device-wasapi",
                          "Mic (WASAPI)");
        QVERIFY(AudioDriverSettings::nameDefaultDriver(settings, windows));
        QCOMPARE(settings.value("Preferences/audio-target").toString(),
                 QString("wasapi"));
        QCOMPARE(AudioDriverSettings::currentImplementation(settings),
                 QString("wasapi"));
        QCOMPARE(settings.value("Preferences/audio-playback-device-wasapi")
                 .toString(), QString("Speakers"));
        QCOMPARE(settings.value("Preferences/audio-record-device-wasapi")
                 .toString(), QString("Mic (WASAPI)"));
        QVERIFY(!settings.contains("Preferences/audio-playback-device-mme"));

        // Named now: another device without a suffix is not carried over
        settings.setValue("Preferences/audio-playback-device", "Headphones");
        QVERIFY(!AudioDriverSettings::nameDefaultDriver(settings, windows));
        QCOMPARE(settings.value("Preferences/audio-playback-device-wasapi")
                 .toString(), QString("Speakers"));

        settings.setValue("Preferences/audio-target", "auto");
        QCOMPARE(AudioDriverSettings::currentImplementation(settings),
                 QString());
        QVERIFY(AudioDriverSettings::nameDefaultDriver(settings, windows));
        QCOMPARE(AudioDriverSettings::currentImplementation(settings),
                 QString("wasapi"));

        // One chosen is left alone, MME included
        settings.setValue("Preferences/audio-target", "mme");
        QVERIFY(!AudioDriverSettings::nameDefaultDriver(settings, windows));
        QCOMPARE(AudioDriverSettings::currentImplementation(settings),
                 QString("mme"));

        // Without WASAPI, MME
        settings.remove("Preferences");
        settings.setValue("Preferences/audio-playback-device", "Speakers");
        QVERIFY(AudioDriverSettings::nameDefaultDriver
                (settings, { "port", "mme", "directsound" }));
        QCOMPARE(AudioDriverSettings::currentImplementation(settings),
                 QString("mme"));
        QCOMPARE(settings.value("Preferences/audio-playback-device-mme")
                 .toString(), QString("Speakers"));

        // Without MME built in, nothing is named, and no device key made
        settings.remove("Preferences");
        settings.setValue("Preferences/audio-playback-device", "Speakers");
        QVERIFY(!AudioDriverSettings::nameDefaultDriver
                (settings, { "pulse", "port", "jack" }));
        QVERIFY(!settings.contains("Preferences/audio-target"));
        QVERIFY(!settings.contains("Preferences/audio-playback-device-mme"));
        QVERIFY(!settings.contains("Preferences/audio-record-device-mme"));
    }

    // Kept per driver, in seconds; 200 ms where none has been chosen, or
    // no driver is named
    void latency_kept_per_driver() {
        std::vector<double> choices = AudioDriverSettings::latencyChoices();
        QCOMPARE(int(choices.size()), 5);
        QCOMPARE(choices.front(), 0.010);
        QCOMPARE(choices.back(), AudioDriverSettings::kDefaultLatency);
        QCOMPARE(AudioDriverSettings::kDefaultLatency, 0.2);

        QSettings settings(m_path, QSettings::IniFormat);
        // WASAPI's own where none is chosen; the others ask for 200 ms
        QCOMPARE(AudioDriverSettings::latency(settings, "wasapi"), 0.02);
        QCOMPARE(AudioDriverSettings::latency(settings, "mme"), 0.2);
        AudioDriverSettings::setLatency(settings, "wasapi", 0.05);
        AudioDriverSettings::setLatency(settings, "mme", 0.1);
        settings.sync();

        QSettings again(m_path, QSettings::IniFormat);
        QCOMPARE(again.value("Preferences/audio-latency-wasapi").toString()
                 .toDouble(), 0.05);
        QCOMPARE(AudioDriverSettings::latency(again, "wasapi"), 0.05);
        QCOMPARE(AudioDriverSettings::latency(again, "mme"), 0.1);
        QCOMPARE(AudioDriverSettings::latency(again, "directsound"), 0.2);
        QCOMPARE(AudioDriverSettings::latency(again, ""), 0.2);

        again.setValue("Preferences/audio-latency-mme", "nonsense");
        QCOMPARE(AudioDriverSettings::latency(again, "mme"), 0.2);
        again.setValue("Preferences/audio-latency-mme", "0");
        QCOMPARE(AudioDriverSettings::latency(again, "mme"), 0.2);
        again.setValue("Preferences/audio-latency-wasapi", "nonsense");
        QCOMPARE(AudioDriverSettings::latency(again, "wasapi"), 0.02);

        QVERIFY(AudioDriverSettings::sameLatency(0.02, 0.0201));
        QVERIFY(!AudioDriverSettings::sameLatency(0.02, 0.01));
    }
};

#endif
