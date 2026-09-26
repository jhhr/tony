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

#ifndef TONY_LATENCY_UTILS_H
#define TONY_LATENCY_UTILS_H

#include "base/BaseTypes.h"

/**
 * Round-trip latency, in frames, to compensate for when a singing
 * take is recorded while the reference is playing: the singer's
 * response to reference frame 0 arrives in the recording at about
 * frame (outputLatency + inputLatency). Either figure may be
 * unavailable (reported as zero or negative); the result is never
 * negative.
 */
inline sv::sv_frame_t
computeRecordingLatency(sv::sv_frame_t outputLatency,
                        sv::sv_frame_t inputLatency)
{
    if (outputLatency < 0) outputLatency = 0;
    if (inputLatency < 0) inputLatency = 0;
    return outputLatency + inputLatency;
}

/**
 * What a take was placed with: the round trip taken off the front of
 * its recording (besides the start gap, below), whether
 * that was a figure the audio check measured (a stored one, or one a
 * check brought for its own run) or the sum of the output
 * and input latencies the device reported, those two latencies, and the
 * rate the device recorded at.  All 0 until known; the round trip stays
 * 0 for a take made without the reference playing, which is placed with
 * none.
 *
 * The round trip is in frames of the recording, which it is taken off.
 * The two reported latencies are in seconds, as MainWindow::roundTripAt()
 * works them out from the frames each counts in (the play source's, the
 * device's), and as it compares them with a stored figure's
 * fingerprint: in frames they count at two different rates when the
 * device's rate is not the session's.
 *
 * The start gap is the rest of what is taken off the front: the frames
 * of the recording made before the reference began to play, also in
 * frames of the recording.  It is estimated when the reference is
 * started, and measured once the audio callback has handed the device
 * its first block; startGapMeasured says whether that happened, as it
 * does for every take that plays the reference.
 */
struct TakeLatency
{
    sv::sv_frame_t roundTrip;
    bool measured;
    double reportedOutput;
    double reportedInput;
    sv::sv_samplerate_t recordingRate;
    sv::sv_frame_t startGap;
    bool startGapMeasured;

    TakeLatency() : roundTrip(0), measured(false), reportedOutput(0),
                    reportedInput(0), recordingRate(0), startGap(0),
                    startGapMeasured(false) { }

    /// Frames of the recording in seconds; 0 while its rate is unknown
    double recordingSeconds(sv::sv_frame_t frames) const {
        return recordingRate > 0 ? double(frames) / recordingRate : 0.0;
    }
};

/**
 * Where to draw a live pitch dot for sound found at the given frame
 * of a take that is going to be shifted earlier by the given latency
 * once it is finished. A negative result means the sound came before
 * the reference started, and the dot should be dropped.
 */
inline sv::sv_frame_t
compensatedLiveFrame(sv::sv_frame_t frame, sv::sv_frame_t latency)
{
    if (latency < 0) latency = 0;
    return frame - latency;
}

#endif
