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

#include "PlaybackSettings.h"

#include <QSettings>

#include <cmath>

namespace PlaybackSettings {
namespace {

QString key(const char *name, int component)
{
    return QString("%1-%2").arg(name).arg(component);
}

// The toggles are kept as QSettings keeps a bool, as they were before
// this unit existed, so that the values a user has already set are read
bool flag(QSettings &settings, QString group, QString key, bool byDefault)
{
    settings.beginGroup(group);
    bool value = settings.value(key, byDefault).toBool();
    settings.endGroup();
    return value;
}

void setFlag(QSettings &settings, QString group, QString key, bool value)
{
    settings.beginGroup(group);
    settings.setValue(key, value);
    settings.endGroup();
}

// Numbers as text, as LatencyCalibration keeps its figures: every
// settings format (the Windows registry too) keeps all of the value
double number(QSettings &settings, QString group, QString key,
              double byDefault)
{
    settings.beginGroup(group);
    bool ok = false;
    double value = settings.value(key, "").toString().toDouble(&ok);
    settings.endGroup();
    if (!ok || !std::isfinite(value)) return byDefault;
    return value;
}

void setNumber(QSettings &settings, QString group, QString key, double value)
{
    settings.beginGroup(group);
    settings.setValue(key, QString::number(value, 'g', 17));
    settings.endGroup();
}

}

bool
visible(QSettings &settings, QString group, int component, bool byDefault)
{
    return flag(settings, group, key("visible", component), byDefault);
}

void
setVisible(QSettings &settings, QString group, int component, bool visible)
{
    setFlag(settings, group, key("visible", component), visible);
}

bool
audible(QSettings &settings, QString group, int component, bool byDefault)
{
    return flag(settings, group, key("audible", component), byDefault);
}

void
setAudible(QSettings &settings, QString group, int component, bool audible)
{
    setFlag(settings, group, key("audible", component), audible);
}

double
gain(QSettings &settings, QString group, int component, double byDefault)
{
    return number(settings, group, key("gain", component), byDefault);
}

void
setGain(QSettings &settings, QString group, int component, double gain)
{
    setNumber(settings, group, key("gain", component), gain);
}

double
pan(QSettings &settings, QString group, int component, double byDefault)
{
    return number(settings, group, key("pan", component), byDefault);
}

void
setPan(QSettings &settings, QString group, int component, double pan)
{
    setNumber(settings, group, key("pan", component), pan);
}

double
masterVolume(QSettings &settings)
{
    return number(settings, kWindowGroup, "mastervolume", 1.0);
}

void
setMasterVolume(QSettings &settings, double volume)
{
    setNumber(settings, kWindowGroup, "mastervolume", volume);
}

bool
backgroundMusicMix(QSettings &settings)
{
    return flag(settings, kWindowGroup, "backgroundmusicmix", true);
}

void
setBackgroundMusicMix(QSettings &settings, bool mix)
{
    setFlag(settings, kWindowGroup, "backgroundmusicmix", mix);
}

double
backgroundMusicGain(QSettings &settings)
{
    return number(settings, kWindowGroup, "backgroundmusicgain", 1.0);
}

void
setBackgroundMusicGain(QSettings &settings, double gain)
{
    setNumber(settings, kWindowGroup, "backgroundmusicgain", gain);
}

double
backgroundMusicPan(QSettings &settings)
{
    return number(settings, kWindowGroup, "backgroundmusicpan", 0.0);
}

void
setBackgroundMusicPan(QSettings &settings, double pan)
{
    setNumber(settings, kWindowGroup, "backgroundmusicpan", pan);
}

}
