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
 * its recording (besides the start gap, which is measured), whether
 * that was a figure the audio check measured or the sum of the output
 * and input latencies the device reported, those two latencies, and the
 * rate the device recorded at.  All 0 until known; the round trip stays
 * 0 for a take made without the reference playing, which is placed with
 * none.
 *
 * In frames as the window has them.  The round trip is taken off the
 * recording, and the input latency is the device's, so both count
 * frames of the recording; but the play source reports the output
 * latency in frames of the session, converted when it resamples to the
 * device (unless the device was opened before the session had a rate;
 * see MainWindow::roundTripAt()).  The two kinds differ only when the
 * device's rate is not the session's; the round trip is worked out in
 * seconds for that reason.
 */
struct TakeLatency
{
    sv::sv_frame_t roundTrip;
    bool measured;
    sv::sv_frame_t reportedOutput;
    sv::sv_frame_t reportedInput;
    sv::sv_samplerate_t recordingRate;

    TakeLatency() : roundTrip(0), measured(false), reportedOutput(0),
                    reportedInput(0), recordingRate(0) { }

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
