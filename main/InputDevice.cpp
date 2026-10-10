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

#include "InputDevice.h"
#include "SettingsKey.h"

#include <QSettings>

namespace InputDevice {
namespace {

const char *const group = "InputDevice";

// AudioDeviceInfo's TYPE_ constants
const int typeWiredHeadset = 3;
const int typeUsbDevice = 11;
const int typeBuiltinMic = 15;
const int typeUsbHeadset = 22;

// The driver as a key
QString keyOf(QString driver)
{
    return SettingsKey::encoded(driver);
}

}

bool
offered(int type)
{
    return type == typeBuiltinMic || type == typeWiredHeadset ||
        type == typeUsbDevice || type == typeUsbHeadset;
}

bool
same(const AudioRoute::Device &a, const AudioRoute::Device &b)
{
    return a.type == b.type &&
        a.productName.trimmed() == b.productName.trimmed();
}

std::vector<AudioRoute::Device>
choices(const std::vector<AudioRoute::Device> &listed)
{
    std::vector<AudioRoute::Device> result;
    for (const AudioRoute::Device &device : listed) {
        if (!offered(device.type)) continue;
        bool seen = false;
        for (const AudioRoute::Device &d : result) {
            if (same(d, device)) seen = true;
        }
        if (!seen) result.push_back(device);
    }
    return result;
}

bool
chosen(QSettings &settings, QString driver, AudioRoute::Device &device)
{
    device = AudioRoute::Device();
    settings.beginGroup(group);
    const QString value = settings.value(keyOf(driver), "").toString();
    settings.endGroup();

    // The type, then the product name, which may hold "|" itself
    const int bar = value.indexOf('|');
    if (bar <= 0) return false;
    bool ok = false;
    const int type = value.left(bar).toInt(&ok);
    if (!ok || type <= 0) return false;
    device.type = type;
    device.productName = value.mid(bar + 1);
    return true;
}

void
choose(QSettings &settings, QString driver, const AudioRoute::Device &device)
{
    settings.beginGroup(group);
    if (device.type <= 0) {
        settings.remove(keyOf(driver));
    } else {
        settings.setValue(keyOf(driver), QString::number(device.type) + "|" +
                          device.productName.trimmed());
    }
    settings.endGroup();
}

int
idToOpen(const std::vector<AudioRoute::Device> &listed,
         const AudioRoute::Device &chosen)
{
    if (chosen.type <= 0) return 0;
    for (const AudioRoute::Device &device : listed) {
        if (same(device, chosen) && device.id > 0) return device.id;
    }
    return 0;
}

}
