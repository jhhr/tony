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

#ifndef TONY_VOICE_THRESHOLD_H
#define TONY_VOICE_THRESHOLD_H

#include "RealtimePitchTracker.h"

#include <QString>

#include <vector>

class QSettings;

/**
 * The voice threshold: a level, in dBFS, under which what the microphone
 * hears is not taken for singing.  It is for a singer who plays the music
 * on speakers, which the microphone hears as well: set above the level
 * the microphone hears the speakers at, the music gives no live dots
 * while recording, and the take's analysis keeps no pitch and no notes
 * where there was only the music.  The audio is recorded as it is.
 *
 * The level is the one the live tracker compares with its own floor
 * (RealtimePitchTracker::kMinLevel): that of the first half of the
 * window a pitch is found in.  A threshold at or below that floor is
 * Off, because the floor is there anyway.
 *
 * Kept as "voicethreshold", in dBFS, in the group "MainWindow": one value
 * for every driver and device, and not saved in the session.
 */
namespace VoiceThreshold
{
    /// The threshold that is Off, and what is read where none is set:
    /// the live tracker's own floor, which is there whatever the threshold
    constexpr double kOff = RealtimePitchTracker::kMinLevel;

    /// Whether a threshold does anything: whether it is above the live
    /// tracker's own floor
    bool isOn(double dbfs);

    /// The level floor the live tracker is given for a threshold: the
    /// higher of the two
    double liveFloor(double dbfs);

    /// The thresholds offered: Off first, then the quietest first
    std::vector<double> choices();

    /// "Off", or the threshold in dBFS ("−40 dBFS", with a minus sign)
    QString label(double dbfs);

    /// The threshold set, or kOff where none is, or where the one set is
    /// at or below kOff or not a number
    double threshold(QSettings &settings);

    /// Set the threshold.  Off is kept as no setting at all, so that it
    /// stays Off whatever the tracker's floor becomes
    void setThreshold(QSettings &settings, double dbfs);
}

#endif
