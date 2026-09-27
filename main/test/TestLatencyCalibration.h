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

    // A phone's route: its driver, and each device by its type and
    // product name, never by its id, which a headset gets anew each time
    // it is plugged in. A device open for playback only has no input yet.
    // Routes stay apart, and a figure comes back under a name that holds
    // the characters the settings take for subgroups
    void route_key_names_the_devices() {
        AudioRoute::Route route;
        route.driver = "oboe";
        route.output.id = 3;
        route.output.type = 2;
        route.output.productName = "Pixel 7";
        route.hasInput = true;
        route.input.id = 7;
        route.input.type = 15;
        route.input.productName = " Pixel 7 ";
        route.rate = 48000;

        Key k = LatencyCalibration::routeKey(route, 48000);
        QCOMPARE(k.implementation, QString("oboe"));
        QCOMPARE(k.playbackDevice, QString("Built-in speaker (Pixel 7)"));
        QCOMPARE(k.recordDevice, QString("Built-in microphone (Pixel 7)"));
        QCOMPARE(k.rate, 48000.0);

        AudioRoute::Route again = route;
        again.output.id = 31;
        again.input.id = 32;
        Key k2 = LatencyCalibration::routeKey(again, 48000);
        QCOMPARE(k2.playbackDevice, k.playbackDevice);
        QCOMPARE(k2.recordDevice, k.recordDevice);

        AudioRoute::Route playbackOnly = route;
        playbackOnly.hasInput = false;
        QCOMPARE(LatencyCalibration::routeKey(playbackOnly, 48000).recordDevice,
                 QString());

        AudioRoute::Device unknown;
        QCOMPARE(AudioRoute::deviceName(unknown), QString("Unknown device"));
        unknown.type = 99;
        QCOMPARE(AudioRoute::deviceName(unknown), QString("Device of type 99"));
        AudioRoute::Device headset;
        headset.type = 8;
        headset.productName = "WH-1000XM4 / Ren's | 100%";
        QCOMPARE(AudioRoute::deviceName(headset),
                 QString("Bluetooth (WH-1000XM4 / Ren's | 100%)"));

        QSettings settings(m_path, QSettings::IniFormat);
        AudioRoute::Route bluetooth = route;
        bluetooth.output = headset;
        const Key kb = LatencyCalibration::routeKey(bluetooth, 48000);
        Figure f = figure(0.21, 0.004, 0.003);
        f.outputStreams = "AAudio, 48000 Hz, Shared, burst 240";
        f.inputStreams = "AAudio (MMAP), 48000 Hz, Exclusive, burst 96";
        LatencyCalibration::store(settings, k, figure(0.03, 0.005, 0.003));
        LatencyCalibration::store(settings, kb, f);
        QCOMPARE(loaded(settings, k), 0.03);
        Figure back;
        QVERIFY(LatencyCalibration::load(settings, kb, back));
        QCOMPARE(back.roundTrip, 0.21);
        QCOMPARE(back.outputStreams, f.outputStreams);
        QCOMPARE(back.inputStreams, f.inputStreams);

        // A desktop's figure keeps no streams
        Figure desk;
        QVERIFY(LatencyCalibration::load(settings, k, desk));
        QCOMPARE(desk.outputStreams, QString());
        QCOMPARE(desk.inputStreams, QString());
    }

    // With the input not open, the one input a figure is kept with for
    // the driver and output at the rate, if there is only one; names that
    // hold what is encoded in a group's name come back whole
    void only_record_device_for_an_output() {
        QSettings settings(m_path, QSettings::IniFormat);
        const QString odd = QString::fromUtf8("Mic 1/2 | \\ 100%2F (\xc3\xa4)");
        LatencyCalibration::store(settings, key("Speaker", odd, 48000, "oboe"),
                                  figure(0.03, 0.005, 0.003));
        LatencyCalibration::store(settings,
                                  key("Headset", "Headset mic", 48000, "oboe"),
                                  figure(0.04, 0.005, 0.003));
        LatencyCalibration::store(settings,
                                  key("Speaker", "Other", 44100, "oboe"),
                                  figure(0.05, 0.005, 0.003));
        LatencyCalibration::store(settings, key("Speaker", "Desk", 48000),
                                  figure(0.06, 0.005, 0.003));

        QString record;
        QVERIFY(LatencyCalibration::onlyRecordDevice
                (settings, key("Speaker", "", 48000, "oboe"), record));
        QCOMPARE(record, odd);
        QVERIFY(LatencyCalibration::onlyRecordDevice
                (settings, key("Headset", "", 48000, "oboe"), record));
        QCOMPARE(record, QString("Headset mic"));
        QVERIFY(LatencyCalibration::onlyRecordDevice
                (settings, key("Speaker", "", 44100, "oboe"), record));
        QCOMPARE(record, QString("Other"));

        record = "unchanged";
        QVERIFY(!LatencyCalibration::onlyRecordDevice
                (settings, key("Speaker", "", 96000, "oboe"), record));
        QVERIFY(!LatencyCalibration::onlyRecordDevice
                (settings, key("Speak", "", 48000, "oboe"), record));
        QVERIFY(!LatencyCalibration::onlyRecordDevice
                (settings, key("Bluetooth", "", 48000, "oboe"), record));
        QCOMPARE(record, QString("unchanged"));

        // Two inputs calibrated with the speaker: which one is not known
        LatencyCalibration::store(settings,
                                  key("Speaker", "USB mic", 48000, "oboe"),
                                  figure(0.07, 0.005, 0.003));
        QVERIFY(!LatencyCalibration::onlyRecordDevice
                (settings, key("Speaker", "", 48000, "oboe"), record));
        QCOMPARE(record, QString("unchanged"));
    }

    // A device that describes its streams is stale when it opened either
    // otherwise, not when the latencies it reports move: Oboe's moved by
    // 4 ms from one take to the next on the phone. A stream it does not
    // describe (the input, before it is opened) is not compared. A figure
    // kept without streams is stale for such a device; and a device that
    // describes none is judged by its latencies as before
    void stale_by_the_streams() {
        Figure f = figure(0.030, 252 / 48000.0, 134 / 48000.0);
        f.outputStreams = "AAudio (MMAP), 48000 Hz, Exclusive, burst 96";
        f.inputStreams = "AAudio (MMAP), 48000 Hz, Exclusive, preset "
            "VoicePerformance";
        const double out = 401 / 48000.0;
        const double in = 222 / 48000.0;

        QVERIFY(!LatencyCalibration::isStale(f, out, in, f.outputStreams,
                                             f.inputStreams));
        QVERIFY(!LatencyCalibration::isStale(f, out, 0.0, f.outputStreams, ""));
        QVERIFY(LatencyCalibration::isStale
                (f, out, in, "AAudio, 48000 Hz, Shared, burst 96",
                 f.inputStreams));
        QVERIFY(LatencyCalibration::isStale
                (f, out, in, f.outputStreams, "AAudio, 48000 Hz, Shared"));
        QVERIFY(LatencyCalibration::isStale
                (f, out, in, "", "AAudio, 48000 Hz, Shared"));

        Figure plain = figure(0.030, 252 / 48000.0, 134 / 48000.0);
        QVERIFY(LatencyCalibration::isStale(plain, 252 / 48000.0,
                                            134 / 48000.0, f.outputStreams,
                                            f.inputStreams));

        // No streams now: the reported pair, to the millisecond
        QVERIFY(LatencyCalibration::isStale(f, out, in));
        QVERIFY(!LatencyCalibration::isStale(f, 252 / 48000.0, 134 / 48000.0));

        InUse measured = LatencyCalibration::roundTripInUse
            (&f, out, in, f.outputStreams, f.inputStreams);
        QVERIFY(measured.source == Source::Measured);
        QCOMPARE(measured.roundTrip, 0.030);
        QCOMPARE(measured.reportedOutput, out);
        InUse other = LatencyCalibration::roundTripInUse
            (&f, out, in, "OpenSLES, 48000 Hz", f.inputStreams);
        QVERIFY(other.source == Source::Reported);
        QVERIFY(other.stale);
        QCOMPARE(other.roundTrip, out + in);
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

    // The stored figure while it is fresh, the reported sum otherwise;
    // and either way the reported pair it was chosen against
    void round_trip_in_use() {
        const double out = 0.2;
        const double in = 0.1;

        InUse none = LatencyCalibration::roundTripInUse(nullptr, out, in);
        QVERIFY(none.source == Source::Reported);
        QCOMPARE(none.roundTrip, out + in);
        QVERIFY(none.date.isNull());
        QVERIFY(!none.stale);
        QCOMPARE(none.reportedOutput, out);
        QCOMPARE(none.reportedInput, in);

        const Figure fresh = figure(0.345, out, in);
        InUse measured = LatencyCalibration::roundTripInUse(&fresh, out, in);
        QVERIFY(measured.source == Source::Measured);
        QCOMPARE(measured.roundTrip, 0.345);
        QCOMPARE(measured.date, fresh.date);
        QVERIFY(!measured.stale);
        QCOMPARE(measured.reportedOutput, out);
        QCOMPARE(measured.reportedInput, in);

        const Figure old = figure(0.345, out + 0.01, in);
        InUse stale = LatencyCalibration::roundTripInUse(&old, out, in);
        QVERIFY(stale.source == Source::Reported);
        QCOMPARE(stale.roundTrip, out + in);
        QVERIFY(stale.date.isNull());
        QVERIFY(stale.stale);
        QCOMPARE(stale.reportedOutput, out);
        QCOMPARE(stale.reportedInput, in);

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
