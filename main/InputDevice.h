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

#ifndef TONY_INPUT_DEVICE_H
#define TONY_INPUT_DEVICE_H

#include "AudioRoute.h"

#include <QString>

#include <vector>

class QSettings;

/**
 * The input device chosen on a driver that lists its inputs, as a
 * phone's does (Android's AudioManager), where the Preferences name no
 * device: Playback > Audio Input Device there.  With none chosen the
 * driver opens the one Android chooses, which for Tony's input preset
 * is a wired headset, then a USB device, then the phone's microphone
 * (docs/port-android.md, "Choosing the input").
 *
 * A choice is kept by the device's type and product name, never its id:
 * Android gives a device another id each time it is plugged in.  In the
 * group "InputDevice", under the driver, as "<type>|<product name>".
 * None chosen is no key at all.
 */
namespace InputDevice
{
    /**
     * Whether a device of this type (AudioDeviceInfo's TYPE_) is offered:
     * the phone's microphone, a wired headset, a USB device or headset.
     * Not a Bluetooth headset's microphone, which Android gives only
     * through a call's link, at call quality both ways; nor what records
     * no microphone (the telephony line, a tuner, a remote submix)
     */
    bool offered(int type);

    /**
     * The devices to offer of those listed, in the order listed: those
     * offered(), and of several with one type and product name (a phone
     * lists each of its microphones) the first
     */
    std::vector<AudioRoute::Device> choices
        (const std::vector<AudioRoute::Device> &listed);

    /// Whether two devices are the same as a choice is kept: by type and
    /// product name
    bool same(const AudioRoute::Device &a, const AudioRoute::Device &b);

    /// The device chosen for the driver, its id 0; false if none is
    bool chosen(QSettings &settings, QString driver,
                AudioRoute::Device &device);

    /// Choose the device for the driver; a type of 0 chooses none
    void choose(QSettings &settings, QString driver,
                const AudioRoute::Device &device);

    /**
     * The id to open the chosen device by, among those listed: that of
     * the first listed device the same as the choice.  0 for none chosen
     * (the driver's own choice), or one chosen that is not listed:
     * unplugged, which the caller says
     */
    int idToOpen(const std::vector<AudioRoute::Device> &listed,
                 const AudioRoute::Device &chosen);
}

#endif
