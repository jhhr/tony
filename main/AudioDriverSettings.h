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

#ifndef TONY_AUDIO_DRIVER_SETTINGS_H
#define TONY_AUDIO_DRIVER_SETTINGS_H

#include <QString>
#include <QStringList>

#include <vector>

class QSettings;

/**
 * The audio driver Tony opens its device through, and the latency it
 * asks that driver for, as the Preferences keep them.
 *
 * A driver is one of bqaudioio's implementations that is PortAudio
 * restricted to one Windows host API: "mme", "directsound", "wasapi".
 * The driver is "audio-target", which MainWindowBase::createAudioIO()
 * reads, and with it the devices, which are kept per driver
 * ("audio-playback-device-<driver>" and "audio-record-device-<driver>").
 * The latency is kept per driver as well, in seconds, as
 * "audio-latency-<driver>".  All in the group "Preferences".
 */
namespace AudioDriverSettings
{
    /// The driver named when none is, where it is built in: the one every
    /// stream went through before there was a choice
    constexpr const char *kDefaultDriver = "mme";

    /// What a driver asks for when no latency has been chosen for it:
    /// what every stream asked for before there was a choice
    constexpr double kDefaultLatency = 0.2;

    /// Those of the implementations given that are drivers, in the order
    /// they are offered in
    QStringList drivers(const QStringList &implementations);

    /// The latencies offered, in seconds, shortest first
    std::vector<double> latencyChoices();

    /// The implementation the Preferences name, "" when none is ("auto"
    /// counts as none, as MainWindowBase::createAudioIO() has it)
    QString currentImplementation(QSettings &settings);

    /// Name the implementation in the Preferences.  The devices chosen for
    /// it are then the ones read
    void setCurrentImplementation(QSettings &settings, QString implementation);

    /// The key of the latency chosen for an implementation, in the group
    /// "Preferences"
    QString latencySettingKey(QString implementation);

    /// The latency chosen for an implementation, in seconds, or
    /// kDefaultLatency where none has been (or none is named)
    double latency(QSettings &settings, QString implementation);

    void setLatency(QSettings &settings, QString implementation,
                    double seconds);

    /// Whether two latencies are the same choice
    bool sameLatency(double a, double b);

    /**
     * Where no implementation is named and kDefaultDriver is among the
     * ones given, name it, and give it the devices chosen before there
     * were drivers (the keys without a suffix) where none are chosen for
     * it.  Once it is named this does nothing again.  True if it named
     * the driver.
     */
    bool nameDefaultDriver(QSettings &settings,
                           const QStringList &implementations);
}

#endif
