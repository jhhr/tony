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

#ifndef TEST_LATENCY_CALIBRATION_H
#define TEST_LATENCY_CALIBRATION_H

// Tier 2: the stored round trip, its key and its staleness, and the
// choice between it and the reported latencies. No window and no
// device. The settings are an INI file of each test's own, in a
// temporary directory, and never the suite's application settings.

#include "../LatencyCalibration.h"
#include "../LatencyUtils.h"

#include <QFile>
#include <QObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class TestLatencyCalibration : public QObject
{
    Q_OBJECT

    typedef LatencyCalibration::Key Key;
    typedef LatencyCalibration::Figure Figure;
    typedef LatencyCalibration::InUse InUse;
    typedef LatencyCalibration::Source Source;

    QTemporaryDir m_dir;
    int m_counter = 0;

    // The settings file of the test that is running
    QString m_path;

    static Key key(QString playback, QString record, double rate = 44100,
                   QString implementation = "") {
        Key k;
        k.implementation = implementation;
        k.playbackDevice = playback;
        k.recordDevice = record;
        k.rate = rate;
        return k;
    }

    static Figure figure(double roundTrip, double output, double input) {
        Figure f;
        f.roundTrip = roundTrip;
        f.spread = 0.0021;
        f.date = QDateTime::fromString("2026-09-26T10:30:15.250Z",
                                       Qt::ISODateWithMs);
        f.reportedOutput = output;
        f.reportedInput = input;
        return f;
    }

    static double loaded(QSettings &settings, const Key &k) {
        Figure f;
        if (!LatencyCalibration::load(settings, k, f)) return -1.0;
        return f.roundTrip;
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

    // What is stored comes back, from the file, with device names that
    // hold the characters QSettings takes as a subgroup, and more; the
    // names make no subgroups of their own; and forget() takes it away
    void stored_figure_comes_back() {
        const Key k = key(QString::fromUtf8("Line 1/2 (Ger\xc3\xa4t f\xc3\xbcr Audio)"),
                          QString::fromUtf8("Mic \\ In | 100% (\xc3\xa4)"));
        const Figure stored = figure(0.2814285714, 8192 / 44100.0,
                                     4096 / 44100.0);
        {
            QSettings settings(m_path, QSettings::IniFormat);
            Figure none;
            QVERIFY(!LatencyCalibration::load(settings, k, none));
            LatencyCalibration::store(settings, k, stored);
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
        }

        QSettings settings(m_path, QSettings::IniFormat);
        Figure f;
        QVERIFY(LatencyCalibration::load(settings, k, f));
        QCOMPARE(f.roundTrip, stored.roundTrip);
        QCOMPARE(f.spread, stored.spread);
        QCOMPARE(f.date, stored.date);
        QCOMPARE(f.reportedOutput, stored.reportedOutput);
        QCOMPARE(f.reportedInput, stored.reportedInput);

        // One group for the devices, one within it for the rate, and
        // nothing more
        settings.beginGroup("LatencyCalibration");
        QCOMPARE(settings.childGroups().size(), 1);
        settings.beginGroup(settings.childGroups().front());
        QCOMPARE(settings.childGroups(), QStringList { "44100" });
        QVERIFY(settings.childKeys().isEmpty());
        settings.endGroup();
        settings.endGroup();

        LatencyCalibration::forget(settings, k);
        QVERIFY(!LatencyCalibration::load(settings, k, f));
        settings.beginGroup("LatencyCalibration");
        QVERIFY(settings.childGroups().isEmpty());
        settings.endGroup();

        // Forgetting what is not there does nothing
        LatencyCalibration::forget(settings, k);
        QVERIFY(!LatencyCalibration::load(settings, k, f));
    }

    // Different devices, driver or rate: each figure is its own. The "|"
    // between the names is not confused with one inside a name
    void keys_stay_apart() {
        QSettings settings(m_path, QSettings::IniFormat);
        const std::vector<Key> keys {
            key("Speakers", "Mic"),
            key("Headphones", "Mic"),
            key("Speakers", "Line In"),
            key("Speakers", "Mic", 48000),
            key("Speakers", "Mic", 44100, "portaudio"),
            key("a|b", "c"),
            key("a", "b|c"),
            key("a/b", "c"),
            key("a", "b/c"),
        };
        for (int i = 0; i < int(keys.size()); ++i) {
            LatencyCalibration::store(settings, keys[i],
                                      figure(0.1 + 0.01 * i, 0.1, 0.05));
        }
        for (int i = 0; i < int(keys.size()); ++i) {
            QCOMPARE(loaded(settings, keys[i]), 0.1 + 0.01 * i);
        }
        QCOMPARE(loaded(settings, key("Speakers", "Mic", 96000)), -1.0);
        QCOMPARE(loaded(settings, key("Speakers", "")), -1.0);

        // Forgetting one leaves the rest, the same devices at another
        // rate among them
        LatencyCalibration::forget(settings, keys[0]);
        QCOMPARE(loaded(settings, keys[0]), -1.0);
        for (int i = 1; i < int(keys.size()); ++i) {
            QCOMPARE(loaded(settings, keys[i]), 0.1 + 0.01 * i);
        }
    }

    // The key names the devices MainWindowBase::createAudioIO() opens:
    // the plain preferences, or, with a driver named, those suffixed
    // with it
    void key_follows_the_preferences() {
        QSettings settings(m_path, QSettings::IniFormat);
        Key k = LatencyCalibration::currentKey(settings, 48000);
        QCOMPARE(k.implementation, QString());
        QCOMPARE(k.playbackDevice, QString());
        QCOMPARE(k.recordDevice, QString());
        QCOMPARE(k.rate, 48000.0);

        settings.setValue("Preferences/audio-playback-device", "Speakers");
        settings.setValue("Preferences/audio-record-device", "Mic");
        k = LatencyCalibration::currentKey(settings, 44100);
        QCOMPARE(k.playbackDevice, QString("Speakers"));
        QCOMPARE(k.recordDevice, QString("Mic"));
        QCOMPARE(k.rate, 44100.0);

        settings.setValue("Preferences/audio-target", "portaudio");
        settings.setValue("Preferences/audio-playback-device-portaudio",
                          "Headphones");
        k = LatencyCalibration::currentKey(settings, 44100);
        QCOMPARE(k.implementation, QString("portaudio"));
        QCOMPARE(k.playbackDevice, QString("Headphones"));
        QCOMPARE(k.recordDevice, QString());
    }

    // Stale when either reported latency has moved by more than the
    // tolerance, either way; not when it has moved by less
    void stale_beyond_the_tolerance() {
        const double out = 8192 / 44100.0;
        const double in = 4096 / 44100.0;
        const Figure f = figure(0.28, out, in);
        const double within = 0.9 * LatencyCalibration::kStaleToleranceSeconds;
        const double beyond = 1.1 * LatencyCalibration::kStaleToleranceSeconds;

        QVERIFY(!LatencyCalibration::isStale(f, out, in));
        for (double sign : { 1.0, -1.0 }) {
            QVERIFY(!LatencyCalibration::isStale(f, out + sign * within, in));
            QVERIFY(!LatencyCalibration::isStale(f, out, in + sign * within));
            QVERIFY(LatencyCalibration::isStale(f, out + sign * beyond, in));
            QVERIFY(LatencyCalibration::isStale(f, out, in + sign * beyond));
        }
    }

    // The stored figure while it is fresh, the reported sum otherwise
    void round_trip_in_use() {
        const double out = 0.2;
        const double in = 0.1;

        InUse none = LatencyCalibration::roundTripInUse(nullptr, out, in);
        QVERIFY(none.source == Source::Reported);
        QCOMPARE(none.roundTrip, out + in);
        QVERIFY(none.date.isNull());
        QVERIFY(!none.stale);

        const Figure fresh = figure(0.345, out, in);
        InUse measured = LatencyCalibration::roundTripInUse(&fresh, out, in);
        QVERIFY(measured.source == Source::Measured);
        QCOMPARE(measured.roundTrip, 0.345);
        QCOMPARE(measured.date, fresh.date);
        QVERIFY(!measured.stale);

        const Figure old = figure(0.345, out + 0.01, in);
        InUse stale = LatencyCalibration::roundTripInUse(&old, out, in);
        QVERIFY(stale.source == Source::Reported);
        QCOMPARE(stale.roundTrip, out + in);
        QVERIFY(stale.date.isNull());
        QVERIFY(stale.stale);

        // A latency reported as less than nothing counts as none
        InUse negative = LatencyCalibration::roundTripInUse(nullptr, -0.5, in);
        QCOMPARE(negative.roundTrip, in);
    }

    // With nothing stored and the device at 44.1 kHz, the round trip in
    // frames is exactly the reported sum, as it was before there was a
    // stored figure. At another rate each latency is converted from the
    // frames it counts: the output's are the session's (bqaudioio's
    // ResamplerWrapper converts them), the input's the device's
    void frames_at_the_recording_rate() {
        const double rate = 44100;
        const std::vector<sv::sv_frame_t> latencies {
            -100, -1, 0, 1, 255, 512, 4096, 8191, 8192, 8820, 12345,
            44100, 88200, 1000003
        };
        for (sv::sv_frame_t out : latencies) {
            for (sv::sv_frame_t in : latencies) {
                double seconds =
                    LatencyCalibration::reportedSeconds(out, rate) +
                    LatencyCalibration::reportedSeconds(in, rate);
                QCOMPARE(LatencyCalibration::toFrames(seconds, rate),
                         computeRecordingLatency(out, in));
            }
        }

        // 8192 frames out and 4096 in at 48 kHz; the play source has the
        // output latency as round(8192 * 44100 / 48000) = 7526 frames
        double seconds = LatencyCalibration::reportedSeconds(7526, rate) +
            LatencyCalibration::reportedSeconds(4096, 48000);
        QVERIFY(std::llabs(LatencyCalibration::toFrames(seconds, 48000) -
                           12288) <= 1);

        QCOMPARE(LatencyCalibration::toFrames(0.0, rate), sv::sv_frame_t(0));
        QCOMPARE(LatencyCalibration::toFrames(-0.1, rate), sv::sv_frame_t(0));
        QCOMPARE(LatencyCalibration::toFrames(0.1, 0), sv::sv_frame_t(0));
        QCOMPARE(LatencyCalibration::reportedSeconds(4096, 0), 0.0);
    }
};

#endif
