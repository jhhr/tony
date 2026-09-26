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

#include "AudioRoute.h"

namespace AudioRoute {

QString
typeName(int type)
{
    // AudioDeviceInfo's TYPE_ constants, API 36
    switch (type) {
    case 1: return "Earpiece";
    case 2: return "Built-in speaker";
    case 3: return "Wired headset";
    case 4: return "Wired headphones";
    case 5: return "Analog line";
    case 6: return "Digital line";
    case 7: return "Bluetooth headset (call)";
    case 8: return "Bluetooth";
    case 9: return "HDMI";
    case 10: return "HDMI ARC";
    case 11: return "USB device";
    case 12: return "USB accessory";
    case 13: return "Dock";
    case 14: return "FM";
    case 15: return "Built-in microphone";
    case 16: return "FM tuner";
    case 17: return "TV tuner";
    case 18: return "Telephony";
    case 19: return "Aux line";
    case 20: return "IP";
    case 21: return "Bus";
    case 22: return "USB headset";
    case 23: return "Hearing aid";
    case 24: return "Built-in speaker (safe)";
    case 25: return "Remote submix";
    case 26: return "Bluetooth LE headset";
    case 27: return "Bluetooth LE speaker";
    case 28: return "Echo reference";
    case 29: return "HDMI eARC";
    case 30: return "Bluetooth LE broadcast";
    case 31: return "Analog dock";
    case 32: return "Multichannel group";
    default: break;
    }
    return QString("Device of type %1").arg(type);
}

QString
deviceName(const Device &device)
{
    // The id is left out: a headset gets another each time it is
    // plugged in, and its figure would be lost with it
    const QString type = (device.type > 0 ? typeName(device.type) :
                          QString("Unknown device"));
    if (device.productName.trimmed() == "") return type;
    return type + " (" + device.productName.trimmed() + ")";
}

}
