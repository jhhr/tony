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

#include "AudioDriverSettings.h"

#include <QSettings>

#include <cmath>

namespace AudioDriverSettings {
namespace {

const char *const preferencesGroup = "Preferences";
const char *const targetKey = "audio-target";

}

QStringList
drivers(const QStringList &implementations)
{
    QStringList result;
    for (QString name : { QString("mme"), QString("directsound"),
                          QString("wasapi") }) {
        if (implementations.contains(name)) result << name;
    }
    return result;
}

std::vector<double>
latencyChoices()
{
    return { 0.010, 0.020, 0.050, 0.100, 0.200 };
}

QString
currentImplementation(QSettings &settings)
{
    settings.beginGroup(preferencesGroup);
    QString implementation = settings.value(targetKey, "").toString();
    settings.endGroup();
    if (implementation == "auto") return "";
    return implementation;
}

void
setCurrentImplementation(QSettings &settings, QString implementation)
{
    settings.beginGroup(preferencesGroup);
    settings.setValue(targetKey, implementation);
    settings.endGroup();
}

QString
latencySettingKey(QString implementation)
{
    return "audio-latency-" + implementation;
}

double
latency(QSettings &settings, QString implementation)
{
    if (implementation == "") return kDefaultLatency;
    settings.beginGroup(preferencesGroup);
    bool ok = false;
    // As text, as LatencyCalibration keeps its figures: every settings
    // format keeps it whole
    double seconds = settings.value(latencySettingKey(implementation), "")
        .toString().toDouble(&ok);
    settings.endGroup();
    if (!ok || !(seconds > 0.0)) return kDefaultLatency;
    return seconds;
}

void
setLatency(QSettings &settings, QString implementation, double seconds)
{
    if (implementation == "") return;
    settings.beginGroup(preferencesGroup);
    settings.setValue(latencySettingKey(implementation),
                      QString::number(seconds, 'g', 17));
    settings.endGroup();
}

bool
sameLatency(double a, double b)
{
    // The choices are whole milliseconds apart
    return std::fabs(a - b) < 0.0005;
}

bool
nameDefaultDriver(QSettings &settings, const QStringList &implementations)
{
    if (currentImplementation(settings) != "") return false;
    if (!implementations.contains(kDefaultDriver)) return false;

    settings.beginGroup(preferencesGroup);
    settings.setValue(targetKey, QString(kDefaultDriver));
    const QString suffix = QString("-") + kDefaultDriver;
    for (QString key : { QString("audio-playback-device"),
                         QString("audio-record-device") }) {
        if (settings.contains(key) && !settings.contains(key + suffix)) {
            settings.setValue(key + suffix, settings.value(key));
        }
    }
    settings.endGroup();
    return true;
}

}
