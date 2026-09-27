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

#include "VoiceThreshold.h"

#include <QCoreApplication>
#include <QSettings>

#include <cmath>

namespace VoiceThreshold {
namespace {

const char *const group = "MainWindow";
const char *const key = "voicethreshold";

QString tr(const char *text)
{
    return QCoreApplication::translate("VoiceThreshold", text);
}

}

bool
isOn(double dbfs)
{
    // Not a number is Off too: the comparison is false
    return dbfs > kOff;
}

double
liveFloor(double dbfs)
{
    return isOn(dbfs) ? dbfs : RealtimePitchTracker::kMinLevel;
}

std::vector<double>
choices()
{
    return { kOff, -50.0, -45.0, -40.0, -35.0, -30.0, -25.0, -20.0, -15.0 };
}

QString
label(double dbfs)
{
    if (!isOn(dbfs)) return tr("Off");
    QString number = QString::number(std::fabs(dbfs), 'g', 6);
    // A minus sign, which a hyphen is too short to read as in a menu
    if (dbfs < 0.0) number = QChar(0x2212) + number;
    return tr("%1 dBFS").arg(number);
}

double
threshold(QSettings &settings)
{
    settings.beginGroup(group);
    bool ok = false;
    // As text, as setThreshold() writes it
    double dbfs = settings.value(key, "").toString().toDouble(&ok);
    settings.endGroup();
    // toDouble() reads "inf" and "nan" too
    if (!ok || !std::isfinite(dbfs) || !isOn(dbfs)) return kOff;
    return dbfs;
}

void
setThreshold(QSettings &settings, double dbfs)
{
    settings.beginGroup(group);
    if (isOn(dbfs) && std::isfinite(dbfs)) {
        // As text, as AudioDriverSettings keeps a latency: every settings
        // format keeps it whole
        settings.setValue(key, QString::number(dbfs, 'g', 17));
    } else {
        settings.remove(key);
    }
    settings.endGroup();
}

}
