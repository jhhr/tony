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

#include "InputChannel.h"

#include <QCoreApplication>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace InputChannel {
namespace {

const char *const group = "InputChannel";

QString tr(const char *text)
{
    return QCoreApplication::translate("InputChannel", text);
}

// A name as part of a key, as LatencyCalibration encodes a device name:
// QSettings takes "/" and "\" as the start of a subgroup, and "|"
// separates the names
QString encoded(QString name)
{
    name.replace("%", "%25");
    name.replace("/", "%2F");
    name.replace("\\", "%5C");
    name.replace("|", "%7C");
    return name;
}

QString keyOf(const Key &key)
{
    return encoded(key.driver) + "|" + encoded(key.recordDevice);
}

QString lastInputKey(QString driver)
{
    return "last-input|" + encoded(driver);
}

bool offered(int channel)
{
    for (int c : choices()) {
        if (c == channel) return true;
    }
    return false;
}

}

std::vector<int>
choices()
{
    return { kBoth, 0, 1 };
}

QString
label(int channel)
{
    if (channel < 0) return tr("Both Inputs");
    return tr("Input %1").arg(channel + 1);
}

int
channel(QSettings &settings, const Key &key)
{
    settings.beginGroup(group);
    bool ok = false;
    const int input = settings.value(keyOf(key), "").toString().toInt(&ok);
    settings.endGroup();
    if (!ok || !offered(input - 1)) return kBoth;
    return input - 1;
}

void
setChannel(QSettings &settings, const Key &key, int channel)
{
    settings.beginGroup(group);
    if (channel >= 0 && offered(channel)) {
        // As text, as the other settings Tony keeps per device are
        settings.setValue(keyOf(key), QString::number(channel + 1));
    } else {
        settings.remove(keyOf(key));
    }
    settings.endGroup();
}

QString
lastInput(QSettings &settings, QString driver)
{
    settings.beginGroup(group);
    const QString input = settings.value(lastInputKey(driver), "").toString();
    settings.endGroup();
    return input;
}

void
setLastInput(QSettings &settings, QString driver, QString input)
{
    settings.beginGroup(group);
    if (settings.value(lastInputKey(driver), "").toString() != input) {
        settings.setValue(lastInputKey(driver), input);
    }
    settings.endGroup();
}

int
effectiveChannel(int chosen, int channels)
{
    if (chosen < 0 || chosen >= channels) return kBoth;
    return chosen;
}

float
peakOf(int channel, float left, float right)
{
    if (channel == 0) return left;
    if (channel == 1) return right;
    return std::max(left, right);
}

std::vector<int>
carrying(const std::vector<float> &peaks, double withinDb)
{
    float top = 0.f;
    for (float peak : peaks) top = std::max(top, peak);
    std::vector<int> inputs;
    if (!(top > 0.f)) return inputs;
    for (size_t c = 0; c < peaks.size(); ++c) {
        if (peaks[c] > 0.f &&
            20.0 * std::log10(double(peaks[c]) / double(top)) >= -withinDb) {
            inputs.push_back(int(c));
        }
    }
    return inputs;
}

}
