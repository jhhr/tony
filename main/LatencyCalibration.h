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

#ifndef TONY_LATENCY_CALIBRATION_H
#define TONY_LATENCY_CALIBRATION_H

#include "base/BaseTypes.h"

#include <QDateTime>
#include <QString>

class QSettings;

/**
 * The round trip the audio check measured, kept for the devices it was
 * measured on, and the choice between it and the latencies the device
 * reports, which is what a take is placed with.
 *
 * A figure is kept per key: the audio driver and the playback and
 * record devices, as the Preferences name them for
 * MainWindowBase::createAudioIO(), which opens them, and the rate the
 * device records at.  Next to it are the latencies the device reported
 * when it was measured.  If the device reports others now, its buffers
 * have changed (in the driver's control panel, say), the round trip
 * has changed with them, and the figure is stale: takes go back to the
 * reported pair until the check is run again.
 *
 * In the settings group "LatencyCalibration", one group for the driver
 * and devices, within it one for the rate.  All times are in seconds.
 */
namespace LatencyCalibration
{
    /// How far either reported latency may move before a stored figure
    /// is stale.  A device reports its latencies in whole frames, and the
    /// output latency is rounded again, to frames of the session, when
    /// the device runs at another rate: both well under this
    constexpr double kStaleToleranceSeconds = 0.001;

    struct Key {
        /// As the Preferences have them, "" for the default
        QString implementation;
        QString playbackDevice;
        QString recordDevice;

        /// The rate the device records at
        sv::sv_samplerate_t rate;

        Key() : rate(0) { }
    };

    /**
     * The key for the devices the Preferences name, read as
     * MainWindowBase::createAudioIO() reads them: "audio-target", then
     * "audio-playback-device" and "audio-record-device", each suffixed
     * with "-" and the driver when one is named.
     */
    Key currentKey(QSettings &settings, sv::sv_samplerate_t recordingRate);

    struct Figure {
        /// What the check measured
        double roundTrip;
        double spread;
        QDateTime date;

        /// What the device reported then: the staleness fingerprint
        double reportedOutput;
        double reportedInput;

        Figure() : roundTrip(0), spread(0),
                   reportedOutput(0), reportedInput(0) { }
    };

    /// Keep the figure for the key, over any kept for it before
    void store(QSettings &settings, const Key &key, const Figure &figure);

    /// The figure kept for the key, if there is one
    bool load(QSettings &settings, const Key &key, Figure &figure);

    /// Drop the figure kept for the key, if there is one
    void forget(QSettings &settings, const Key &key);

    /// Whether either latency the device reports now differs from the
    /// one it reported when the figure was measured by more than
    /// kStaleToleranceSeconds
    bool isStale(const Figure &figure,
                 double reportedOutput, double reportedInput);

    enum class Source {
        Reported, ///< the device's reported output and input latency
        Measured  ///< a figure the audio check measured
    };

    const char *sourceName(Source source);

    /// The round trip a take is placed with, and where it came from
    struct InUse {
        Source source;
        double roundTrip;

        /// When it was measured; null for the reported pair
        QDateTime date;

        /// A figure was stored for the key, but it is stale
        bool stale;

        InUse() : source(Source::Reported), roundTrip(0), stale(false) { }
    };

    /**
     * The stored figure, if there is one and it is not stale; otherwise
     * the sum of the two reported latencies.  stored may be null.
     */
    InUse roundTripInUse(const Figure *stored,
                         double reportedOutput, double reportedInput);

    /// A reported latency, counted in frames at the given rate, in
    /// seconds.  A latency reported as zero or less, or a rate not yet
    /// known, counts as none
    double reportedSeconds(sv::sv_frame_t frames, sv::sv_samplerate_t rate);

    /// Seconds in whole frames at the given rate, rounded to the nearest
    sv::sv_frame_t toFrames(double seconds, sv::sv_samplerate_t rate);
}

#endif
