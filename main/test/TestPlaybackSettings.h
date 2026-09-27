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

#ifndef TEST_PLAYBACK_SETTINGS_H
#define TEST_PLAYBACK_SETTINGS_H

// Tier 2: how the toolbar's tracks are shown and played, and the master
// volume, as the settings keep them between launches. No window and no
// analyser. The settings
// are an INI file of each test's own, as TestAudioDriverSettings' are.

#include "../PlaybackSettings.h"

#include <QFile>
#include <QObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class TestPlaybackSettings : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_counter = 0;
    QString m_path;

    const QString reference = PlaybackSettings::kReferenceGroup;
    const QString singing = PlaybackSettings::kSingingGroup;

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

    // The keys are the ones Analyser has always read, in the group it
    // has always used for the reference, and the singing track has a
    // group of its own. The toggles are kept as bools, the numbers as
    // text, and nothing is left open in the settings object
    void keys_named_by_group_and_track() {
        QCOMPARE(reference, QString("Analyser"));
        QCOMPARE(singing, QString("SingingAnalyser"));

        QSettings settings(m_path, QSettings::IniFormat);
        PlaybackSettings::setVisible(settings, reference, 3, true);
        PlaybackSettings::setAudible(settings, reference, 2, false);
        PlaybackSettings::setGain(settings, reference, 1, 0.25);
        PlaybackSettings::setPan(settings, singing, 0, -0.5);
        QCOMPARE(settings.group(), QString());

        QCOMPARE(settings.value("Analyser/visible-3").typeId(),
                 int(QMetaType::Bool));
        QCOMPARE(settings.value("Analyser/audible-2").typeId(),
                 int(QMetaType::Bool));
        QCOMPARE(settings.value("Analyser/gain-1").typeId(),
                 int(QMetaType::QString));
        QCOMPARE(settings.value("SingingAnalyser/pan-0").typeId(),
                 int(QMetaType::QString));
        settings.sync();

        QSettings again(m_path, QSettings::IniFormat);
        QCOMPARE(again.allKeys().size(), 4);
        QCOMPARE(again.value("Analyser/visible-3").toBool(), true);
        QCOMPARE(again.value("Analyser/audible-2").toBool(), false);
        QCOMPARE(again.value("Analyser/gain-1").toString(), QString("0.25"));
        QCOMPARE(again.value("SingingAnalyser/pan-0").toString(),
                 QString("-0.5"));
    }

    // What the caller says, for a key never written, or one that holds
    // no number, or another track's
    void defaults_where_nothing_is_kept() {
        QSettings settings(m_path, QSettings::IniFormat);
        for (bool byDefault : { true, false }) {
            QCOMPARE(PlaybackSettings::visible(settings, reference, 0,
                                               byDefault), byDefault);
            QCOMPARE(PlaybackSettings::audible(settings, singing, 0,
                                               byDefault), byDefault);
        }
        QCOMPARE(PlaybackSettings::gain(settings, reference, 1, 0.5), 0.5);
        QCOMPARE(PlaybackSettings::pan(settings, reference, 1, -1.0), -1.0);

        PlaybackSettings::setVisible(settings, reference, 1, false);
        PlaybackSettings::setGain(settings, reference, 1, 0.75);
        QCOMPARE(PlaybackSettings::visible(settings, reference, 2, true),
                 true);
        QCOMPARE(PlaybackSettings::gain(settings, reference, 2, 0.5), 0.5);

        for (const char *text : { "nonsense", "", "nan", "inf" }) {
            settings.setValue("Analyser/gain-0", text);
            settings.setValue("Analyser/pan-0", text);
            QCOMPARE(PlaybackSettings::gain(settings, reference, 0, 1.0),
                     1.0);
            QCOMPARE(PlaybackSettings::pan(settings, reference, 0, -1.0),
                     -1.0);
        }
    }

    // Each value comes back exactly from the file, a float's included,
    // and the values kept before this unit existed are read as they are
    void round_trip() {
        const float level = 0.562f; // a notch of the toolbar's control
        {
            QSettings settings(m_path, QSettings::IniFormat);
            PlaybackSettings::setVisible(settings, singing, 1, false);
            PlaybackSettings::setAudible(settings, singing, 0, false);
            PlaybackSettings::setGain(settings, reference, 0, level);
            PlaybackSettings::setGain(settings, reference, 2, 0.0);
            PlaybackSettings::setPan(settings, reference, 1, -0.25);
            PlaybackSettings::setPan(settings, reference, 2, 1.0);
            settings.setValue("Analyser/visible-0", false);
            settings.setValue("Analyser/audible-1", false);
        }

        QSettings settings(m_path, QSettings::IniFormat);
        QCOMPARE(PlaybackSettings::visible(settings, singing, 1, true),
                 false);
        QCOMPARE(PlaybackSettings::audible(settings, singing, 0, true),
                 false);
        QCOMPARE(float(PlaybackSettings::gain(settings, reference, 0, 1.0)),
                 level);
        QCOMPARE(PlaybackSettings::gain(settings, reference, 2, 0.5), 0.0);
        QCOMPARE(PlaybackSettings::pan(settings, reference, 1, 1.0), -0.25);
        QCOMPARE(PlaybackSettings::pan(settings, reference, 2, 0.0), 1.0);
        QCOMPARE(PlaybackSettings::visible(settings, reference, 0, true),
                 false);
        QCOMPARE(PlaybackSettings::audible(settings, reference, 1, true),
                 false);

        // and turned back on
        PlaybackSettings::setVisible(settings, singing, 1, true);
        PlaybackSettings::setAudible(settings, reference, 1, true);
        QCOMPARE(PlaybackSettings::visible(settings, singing, 1, false),
                 true);
        QCOMPARE(PlaybackSettings::audible(settings, reference, 1, false),
                 true);
    }

    // What one group holds is not read in the other, and a write to one
    // leaves the other as it was
    void groups_kept_apart() {
        QSettings settings(m_path, QSettings::IniFormat);
        PlaybackSettings::setVisible(settings, singing, 1, false);
        PlaybackSettings::setAudible(settings, singing, 0, false);
        PlaybackSettings::setGain(settings, singing, 0, 0.3);
        PlaybackSettings::setPan(settings, singing, 0, 0.4);

        QCOMPARE(PlaybackSettings::visible(settings, reference, 1, true),
                 true);
        QCOMPARE(PlaybackSettings::audible(settings, reference, 0, true),
                 true);
        QCOMPARE(PlaybackSettings::gain(settings, reference, 0, 1.0), 1.0);
        QCOMPARE(PlaybackSettings::pan(settings, reference, 0, -1.0), -1.0);
        settings.beginGroup(reference);
        QVERIFY(settings.childKeys().isEmpty());
        settings.endGroup();

        PlaybackSettings::setAudible(settings, reference, 0, true);
        PlaybackSettings::setVisible(settings, reference, 1, true);
        PlaybackSettings::setGain(settings, reference, 0, 0.9);
        QCOMPARE(PlaybackSettings::audible(settings, singing, 0, true),
                 false);
        QCOMPARE(PlaybackSettings::visible(settings, singing, 1, true),
                 false);
        QCOMPARE(PlaybackSettings::gain(settings, singing, 0, 1.0), 0.3);
        QCOMPARE(PlaybackSettings::pan(settings, singing, 0, 0.0), 0.4);
    }

    // The master volume and the background music are the window's, in
    // the group of its other options; the mix is a bool, the numbers
    // text, as the tracks' are
    void window_keys() {
        QCOMPARE(QString(PlaybackSettings::kWindowGroup),
                 QString("MainWindow"));

        QSettings settings(m_path, QSettings::IniFormat);
        PlaybackSettings::setMasterVolume(settings, 0.5);
        PlaybackSettings::setBackgroundMusicMix(settings, false);
        PlaybackSettings::setBackgroundMusicGain(settings, 0.25);
        PlaybackSettings::setBackgroundMusicPan(settings, -0.75);
        QCOMPARE(settings.group(), QString());
        QCOMPARE(settings.value("MainWindow/backgroundmusicmix").typeId(),
                 int(QMetaType::Bool));
        for (const char *key : { "MainWindow/mastervolume",
                                 "MainWindow/backgroundmusicgain",
                                 "MainWindow/backgroundmusicpan" }) {
            QCOMPARE(settings.value(key).typeId(), int(QMetaType::QString));
        }
        settings.sync();

        QSettings again(m_path, QSettings::IniFormat);
        QCOMPARE(again.allKeys().size(), 4);
        QCOMPARE(again.value("MainWindow/mastervolume").toString(),
                 QString("0.5"));
        QCOMPARE(again.value("MainWindow/backgroundmusicmix").toBool(),
                 false);
        QCOMPARE(again.value("MainWindow/backgroundmusicgain").toString(),
                 QString("0.25"));
        QCOMPARE(again.value("MainWindow/backgroundmusicpan").toString(),
                 QString("-0.75"));
    }

    // Full volume, mixed in, at its own level in the middle: as a window
    // was before any of it was kept. So too for a key that holds no
    // number, and the reference's own gain and pan are not these
    void window_defaults() {
        QSettings settings(m_path, QSettings::IniFormat);
        QCOMPARE(PlaybackSettings::masterVolume(settings), 1.0);
        QCOMPARE(PlaybackSettings::backgroundMusicMix(settings), true);
        QCOMPARE(PlaybackSettings::backgroundMusicGain(settings), 1.0);
        QCOMPARE(PlaybackSettings::backgroundMusicPan(settings), 0.0);

        PlaybackSettings::setGain(settings, reference, 0, 0.3);
        PlaybackSettings::setPan(settings, reference, 0, -1.0);
        QCOMPARE(PlaybackSettings::backgroundMusicGain(settings), 1.0);
        QCOMPARE(PlaybackSettings::backgroundMusicPan(settings), 0.0);

        for (const char *text : { "nonsense", "", "nan", "inf" }) {
            settings.setValue("MainWindow/mastervolume", text);
            settings.setValue("MainWindow/backgroundmusicgain", text);
            settings.setValue("MainWindow/backgroundmusicpan", text);
            QCOMPARE(PlaybackSettings::masterVolume(settings), 1.0);
            QCOMPARE(PlaybackSettings::backgroundMusicGain(settings), 1.0);
            QCOMPARE(PlaybackSettings::backgroundMusicPan(settings), 0.0);
        }
    }

    // Each comes back exactly, the fader's float and the level control's
    // included, and each is written alone
    void window_round_trip() {
        const float volume = 0.7079458f; // -3 dB, as the fader keeps it
        const float level = 0.562f;
        {
            QSettings settings(m_path, QSettings::IniFormat);
            PlaybackSettings::setMasterVolume(settings, volume);
            PlaybackSettings::setBackgroundMusicMix(settings, false);
            PlaybackSettings::setBackgroundMusicGain(settings, level);
            PlaybackSettings::setBackgroundMusicPan(settings, 0.25);
        }

        QSettings settings(m_path, QSettings::IniFormat);
        QCOMPARE(float(PlaybackSettings::masterVolume(settings)), volume);
        QCOMPARE(PlaybackSettings::backgroundMusicMix(settings), false);
        QCOMPARE(float(PlaybackSettings::backgroundMusicGain(settings)),
                 level);
        QCOMPARE(PlaybackSettings::backgroundMusicPan(settings), 0.25);

        // Mixed in again, and silenced: the rest stays as it was
        PlaybackSettings::setBackgroundMusicMix(settings, true);
        PlaybackSettings::setMasterVolume(settings, 0.0);
        QCOMPARE(PlaybackSettings::backgroundMusicMix(settings), true);
        QCOMPARE(PlaybackSettings::masterVolume(settings), 0.0);
        QCOMPARE(float(PlaybackSettings::backgroundMusicGain(settings)),
                 level);
        QCOMPARE(PlaybackSettings::backgroundMusicPan(settings), 0.25);
    }
};

#endif
