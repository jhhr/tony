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

#ifndef TEST_VOICE_THRESHOLD_H
#define TEST_VOICE_THRESHOLD_H

// Tier 2: the voice threshold as the settings keep it, the choices
// offered, and the live tracker's floor it makes. No window and no
// device. The settings are an INI file of each test's own, as
// TestAudioDriverSettings' are.

#include "../VoiceThreshold.h"
#include "../RealtimePitchTracker.h"

#include <QFile>
#include <QObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <limits>

class TestVoiceThreshold : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_counter = 0;
    QString m_path;

    static QString minus() { return QString(QChar(0x2212)); }

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

    // Off is the live tracker's own floor, so that a threshold that is
    // Off leaves the floor where it is
    void off_is_the_trackers_floor() {
        QCOMPARE(VoiceThreshold::kOff, RealtimePitchTracker::kMinLevel);
        QCOMPARE(VoiceThreshold::kOff, -60.0);
        QVERIFY(!VoiceThreshold::isOn(VoiceThreshold::kOff));
        QVERIFY(!VoiceThreshold::isOn(-70.0));
        QVERIFY(!VoiceThreshold::isOn(std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(VoiceThreshold::isOn(-59.5));
        QVERIFY(VoiceThreshold::isOn(-40.0));
    }

    // Nothing set is Off
    void absent_is_off() {
        QSettings settings(m_path, QSettings::IniFormat);
        QCOMPARE(VoiceThreshold::threshold(settings), VoiceThreshold::kOff);
        QVERIFY(!VoiceThreshold::isOn(VoiceThreshold::threshold(settings)));
    }

    // At the floor, or below it, is Off; above it, however little, is on
    void at_or_below_the_floor_is_off() {
        QSettings settings(m_path, QSettings::IniFormat);

        settings.setValue("MainWindow/voicethreshold", "-60");
        QCOMPARE(VoiceThreshold::threshold(settings), VoiceThreshold::kOff);

        settings.setValue("MainWindow/voicethreshold", "-75");
        QCOMPARE(VoiceThreshold::threshold(settings), VoiceThreshold::kOff);

        settings.setValue("MainWindow/voicethreshold", "-59.5");
        QCOMPARE(VoiceThreshold::threshold(settings), -59.5);
    }

    // A choice written is read back, from the key the spec names, by
    // another QSettings on the same file
    void a_choice_is_written_and_read_back() {
        {
            QSettings settings(m_path, QSettings::IniFormat);
            VoiceThreshold::setThreshold(settings, -40.0);
            settings.sync();
        }
        QSettings again(m_path, QSettings::IniFormat);
        QCOMPARE(again.value("MainWindow/voicethreshold").toString()
                 .toDouble(), -40.0);
        QCOMPARE(VoiceThreshold::threshold(again), -40.0);
        QVERIFY(VoiceThreshold::isOn(VoiceThreshold::threshold(again)));

        // Off is written as no setting at all
        VoiceThreshold::setThreshold(again, VoiceThreshold::kOff);
        QVERIFY(!again.contains("MainWindow/voicethreshold"));
        QCOMPARE(VoiceThreshold::threshold(again), VoiceThreshold::kOff);

        VoiceThreshold::setThreshold(again, -25.0);
        VoiceThreshold::setThreshold(again, -80.0);
        QVERIFY(!again.contains("MainWindow/voicethreshold"));
    }

    // A value that is not one of the choices is used as it is; one that
    // is not a number is Off
    void an_odd_value() {
        QSettings settings(m_path, QSettings::IniFormat);

        settings.setValue("MainWindow/voicethreshold", "-37.5");
        QCOMPARE(VoiceThreshold::threshold(settings), -37.5);

        VoiceThreshold::setThreshold(settings, -42.25);
        QCOMPARE(VoiceThreshold::threshold(settings), -42.25);

        for (QString odd : { QString("nonsense"), QString(""),
                             QString("nan"), QString("inf"),
                             QString("-inf") }) {
            settings.setValue("MainWindow/voicethreshold", odd);
            QVERIFY2(VoiceThreshold::threshold(settings) ==
                     VoiceThreshold::kOff, qPrintable(odd));
        }
    }

    // The choices of the spec, Off first, each with its label
    void choices_and_their_labels() {
        const std::vector<double> choices = VoiceThreshold::choices();
        const std::vector<double> expected =
            { VoiceThreshold::kOff, -50, -45, -40, -35, -30, -25, -20, -15 };
        QCOMPARE(choices, expected);

        QCOMPARE(VoiceThreshold::label(VoiceThreshold::kOff), QString("Off"));
        QCOMPARE(VoiceThreshold::label(-70.0), QString("Off"));
        QCOMPARE(VoiceThreshold::label(-40.0), minus() + "40 dBFS");
        QCOMPARE(VoiceThreshold::label(-15.0), minus() + "15 dBFS");
        QCOMPARE(VoiceThreshold::label(-37.5), minus() + "37.5 dBFS");

        QStringList labels;
        for (double c : choices) labels << VoiceThreshold::label(c);
        labels.removeDuplicates();
        QCOMPARE(int(labels.size()), int(choices.size()));
    }

    // The live tracker's floor is the higher of its own and the
    // threshold
    void the_live_floor() {
        const double floor = RealtimePitchTracker::kMinLevel;
        QCOMPARE(VoiceThreshold::liveFloor(VoiceThreshold::kOff), floor);
        QCOMPARE(VoiceThreshold::liveFloor(-80.0), floor);
        QCOMPARE(VoiceThreshold::liveFloor
                 (std::numeric_limits<double>::quiet_NaN()), floor);
        QCOMPARE(VoiceThreshold::liveFloor(-59.5), -59.5);
        QCOMPARE(VoiceThreshold::liveFloor(-40.0), -40.0);
        QCOMPARE(VoiceThreshold::liveFloor(-15.0), -15.0);
    }
};

#endif
