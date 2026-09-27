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

#ifndef TONY_AUDIO_ROUTE_H
#define TONY_AUDIO_ROUTE_H

#include "base/BaseTypes.h"

#include <QString>

/**
 * The route an audio device opened, where the device can say, as
 * Oboe's can on a phone: which output and input it plays and records
 * through (the speaker, a wired or USB headset, Bluetooth) and how it
 * opened its streams. The Preferences name no device on a phone, which
 * opens whatever route it has, and a Bluetooth route is 100 to 200 ms
 * longer than the speaker's: a measured round trip is kept for the
 * route, and is out of date once the streams open otherwise
 * (LatencyCalibration).
 */
namespace AudioRoute
{
    /// A device as Android's AudioManager describes it (AudioDeviceInfo)
    struct Device {
        /// What AAudio opens it by. A device plugged in again gets
        /// another, so it is logged but names nothing
        int id;

        /// One of AudioDeviceInfo's TYPE_ constants; 0 if not known
        int type;

        /// Its product name: the phone's model for its own speaker and
        /// microphone, a headset's name for a Bluetooth one
        QString productName;

        Device() : id(0), type(0) { }
    };

    struct Route {
        /// The driver that reports it; "" for a device that reports no
        /// route, whose devices the Preferences name (the desktop's)
        QString driver;

        Device output;

        /// The input, while it is open; hasInput is false for a device
        /// opened for playback only
        bool hasInput;
        Device input;

        /// The rate both streams run at
        sv::sv_samplerate_t rate;

        /// How each stream was opened (the audio API, shared or
        /// exclusive, its burst and buffer), on which its latency
        /// depends; "" for a stream not open
        QString outputStreams;
        QString inputStreams;

        Route() : hasInput(false), rate(0) { }
    };

    /// A type of device in a few words, as AudioDeviceInfo's TYPE_
    /// constant of that number names it; "device of type N" for one
    /// this does not know
    QString typeName(int type);

    /// A device as a figure is kept for it and the user is told of it:
    /// its type, and its product name if it has one, never its id
    QString deviceName(const Device &device);
}

/**
 * An audio device that knows its route (OboeAudioIO; the tests' fake,
 * when told one). The window asks for it by casting the device it
 * opened; a device that is not one reports no route.
 */
class AudioRouteReporter
{
public:
    virtual ~AudioRouteReporter() { }

    /// The route as it was opened: its driver is "" if the device
    /// cannot say
    virtual AudioRoute::Route getAudioRoute() const = 0;
};

#endif
